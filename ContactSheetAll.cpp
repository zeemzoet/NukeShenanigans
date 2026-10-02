// ContactSheetAll.cpp
//
// A ContactSheet that tiles every input into a grid and carries *all* channels
// (or any user-selected ChannelSet) through, instead of only rgba.
//
// Resampling is an exact area-average (box) filter, done separably per row:
// each output pixel integrates the input pixels its footprint covers, with
// fractional weights at the edges. Cost scales with input pixels touched,
// which is optimal for the downscaling a contact sheet normally does.

#include "DDImage/Iop.h"
#include "DDImage/Row.h"
#include "DDImage/Knob.h"
#include "DDImage/Knobs.h"
#include "DDImage/Format.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace DD::Image;

namespace {

const char* const kClass = "ContactSheetAll";
const char* const kHelp =
    "ContactSheetAll\n\n"
    "Tiles all inputs into a grid, processing every channel in 'channels' "
    "(default: all). Uses an area-average filter for resizing.";

enum RowOrder { kTopBottom, kBottomTop };
enum ColOrder { kLeftRight, kRightLeft };
const char* const kRowOrderNames[] = { "TopBottom", "BottomTop", nullptr };
const char* const kColOrderNames[] = { "LeftRight", "RightLeft", nullptr };

// Placement of one input inside the output.
// Input coordinate for output pixel p:  u = fx + (p - ox) / sx
struct Cell
{
    int    input;
    double ox, oy;        // output position of the input format's bottom-left
    double sx, sy;        // output pixels per input pixel
    int    fx, fy, fr, ft; // input format box
    int    x, y, r, t;    // output pixels touched (rounded outward)
};

} // namespace

class ContactSheetAll : public Iop
{
public:
    explicit ContactSheetAll(Node* node) : Iop(node) { _formats.format(nullptr); }

    int minimum_inputs() const override { return 1; }
    int maximum_inputs() const override { return 10000; }

    const char* input_label(int n, char* buf) const override
    {
        std::snprintf(buf, 16, "%d", n + 1);
        return buf;
    }

    void knobs(Knob_Callback f) override
    {
        Format_knob(f, &_formats, "format", "format");
        ChannelMask_knob(f, &_channels, "channels", "channels");
        Bool_knob(f, &_autoLayout, "auto_layout", "auto layout");
        Tooltip(f, "Pick rows/columns automatically from the number of inputs.");
        Int_knob(f, &_rows, "rows", "rows");
        Int_knob(f, &_columns, "columns", "columns");
        ClearFlags(f, Knob::STARTLINE);
        Float_knob(f, &_gap, "gap", "gap");
        SetRange(f, 0, 100);
        Enumeration_knob(f, &_rowOrder, kRowOrderNames, "roworder", "row order");
        Enumeration_knob(f, &_colOrder, kColOrderNames, "colorder", "column order");
        ClearFlags(f, Knob::STARTLINE);
        Bool_knob(f, &_preserveAspect, "preserve_aspect", "preserve aspect");
        Tooltip(f, "Fit each input inside its cell keeping its aspect ratio, centred.");
    }

    int knob_changed(Knob* k) override
    {
        if (k == &Knob::showPanel || k->is("auto_layout")) {
            knob("rows")->enable(!_autoLayout);
            knob("columns")->enable(!_autoLayout);
            return 1;
        }
        return Iop::knob_changed(k);
    }

    const char* Class() const override { return kClass; }
    const char* node_help() const override { return kHelp; }
    static const Iop::Description description;

protected:
    void _validate(bool forReal) override
    {
        copy_info(); // frame range, etc. from input 0

        ChannelSet chans;
        int first = info_.first_frame(), last = info_.last_frame();
        for (int i = 0; i < inputs(); ++i) {
            Iop* in = input(i);
            in->validate(forReal);
            chans += in->channels();
            first = std::min(first, in->info().first_frame());
            last  = std::max(last,  in->info().last_frame());
        }
        chans &= _channels;

        const Format& fmt = *_formats.format();
        info_.format(fmt);
        info_.full_size_format(*_formats.fullSizeFormat());
        info_.set(fmt.x(), fmt.y(), fmt.r(), fmt.t());
        info_.first_frame(first);
        info_.last_frame(last);
        info_.channels(chans);
        set_out_channels(chans);

        buildLayout(fmt);
    }

    void _request(int x, int y, int r, int t, ChannelMask channels, int count) override
    {
        for (const Cell& c : _cells) {
            const int ox0 = std::max(x, c.x), ox1 = std::min(r, c.r);
            const int oy0 = std::max(y, c.y), oy1 = std::min(t, c.t);
            if (ox0 >= ox1 || oy0 >= oy1)
                continue;

            const int ix0 = std::max(c.fx, int(std::floor(c.fx + (ox0 - c.ox) / c.sx)));
            const int ix1 = std::min(c.fr, int(std::ceil (c.fx + (ox1 - c.ox) / c.sx)));
            const int iy0 = std::max(c.fy, int(std::floor(c.fy + (oy0 - c.oy) / c.sy)));
            const int iy1 = std::min(c.ft, int(std::ceil (c.fy + (oy1 - c.oy) / c.sy)));
            if (ix0 >= ix1 || iy0 >= iy1)
                continue;

            Iop* in = input(c.input);
            ChannelSet ch(channels);
            ch &= in->channels();
            in->request(ix0, iy0, ix1, iy1, ch, count);
        }
    }

    void engine(int y, int x, int r, ChannelMask channels, Row& row) override
    {
        foreach (z, channels)
            std::fill_n(row.writable(z) + x, r - x, 0.0f);

        // Per-output-pixel horizontal footprint: col/wgt[start[i] .. start[i+1])
        std::vector<int>   start, col;
        std::vector<float> wgt;

        for (const Cell& c : _cells) {
            if (y < c.y || y >= c.t)
                continue;
            const int x0 = std::max(x, c.x), x1 = std::min(r, c.r);
            if (x0 >= x1)
                continue;

            Iop* in = input(c.input);
            ChannelSet ch(channels);
            ch &= in->channels();
            if (ch.empty())
                continue;

            // Vertical footprint of this output row, clipped to the input format.
            const double v0 = std::max<double>(c.fy, c.fy + (y     - c.oy) / c.sy);
            const double v1 = std::min<double>(c.ft, c.fy + (y + 1 - c.oy) / c.sy);
            if (v0 >= v1)
                continue;

            // Horizontal footprints.
            const int n = x1 - x0;
            start.assign(1, 0);
            col.clear();
            wgt.clear();
            for (int i = 0; i < n; ++i) {
                const double u0 = std::max<double>(c.fx, c.fx + (x0 + i     - c.ox) / c.sx);
                const double u1 = std::min<double>(c.fr, c.fx + (x0 + i + 1 - c.ox) / c.sx);
                for (int k = int(std::floor(u0)); k < u1; ++k) {
                    const double w = std::min(u1, k + 1.0) - std::max(u0, double(k));
                    if (w > 0.0) {
                        col.push_back(k);
                        wgt.push_back(float(w));
                    }
                }
                start.push_back(int(col.size()));
            }
            if (col.empty())
                continue;

            // Columns are monotonic, so the needed input span is front..back.
            const int cMin = col.front();
            const int cMax = col.back() + 1;

            // Normalise by the full (unclipped) footprint area so partially
            // covered edge pixels fade to black: antialiased tile borders.
            const double norm = c.sx * c.sy;

            Row inRow(cMin, cMax);
            for (int vy = int(std::floor(v0)); vy < v1; ++vy) {
                const float wy = float((std::min(v1, vy + 1.0) - std::max(v0, double(vy))) * norm);
                if (wy <= 0.0f)
                    continue;

                in->get(vy, cMin, cMax, ch, inRow);
                if (aborted())
                    return;

                foreach (z, ch) {
                    const float* src = inRow[z];
                    float*       dst = row.writable(z) + x0;
                    for (int i = 0; i < n; ++i) {
                        float s = 0.0f;
                        for (int j = start[i]; j < start[i + 1]; ++j)
                            s += wgt[j] * src[col[j]];
                        dst[i] += wy * s;
                    }
                }
            }
        }
    }

private:
    void buildLayout(const Format& fmt)
    {
        _cells.clear();

        const int n = inputs();
        int cols = _columns, rows = _rows;
        if (_autoLayout) {
            cols = int(std::ceil(std::sqrt(double(n))));
            rows = (n + cols - 1) / cols;
        }
        cols = std::max(cols, 1);
        rows = std::max(rows, 1);

        const double gap   = std::max(0.0f, _gap);
        const double cellW = (fmt.w() - gap * (cols + 1)) / cols;
        const double cellH = (fmt.h() - gap * (rows + 1)) / rows;
        if (cellW <= 0.0 || cellH <= 0.0)
            return;

        const int count = std::min(n, rows * cols);
        _cells.reserve(count);

        for (int i = 0; i < count; ++i) {
            const Format& inFmt = input(i)->format();
            if (inFmt.w() <= 0 || inFmt.h() <= 0)
                continue;

            int c = i % cols;
            int r = i / cols;
            if (_colOrder == kRightLeft) c = cols - 1 - c;
            if (_rowOrder == kTopBottom) r = rows - 1 - r; // row index from bottom

            const double cx = fmt.x() + gap + c * (cellW + gap);
            const double cy = fmt.y() + gap + r * (cellH + gap);

            double sx = cellW / inFmt.w();
            double sy = cellH / inFmt.h();
            double ox = cx, oy = cy;

            if (_preserveAspect) {
                // Input width expressed in output pixels at unit scale.
                const double aspect = inFmt.pixel_aspect() / fmt.pixel_aspect();
                const double s = std::min(cellW / (inFmt.w() * aspect), cellH / inFmt.h());
                sx = s * aspect;
                sy = s;
                ox = cx + 0.5 * (cellW - inFmt.w() * sx);
                oy = cy + 0.5 * (cellH - inFmt.h() * sy);
            }

            Cell cell;
            cell.input = i;
            cell.ox = ox; cell.oy = oy;
            cell.sx = sx; cell.sy = sy;
            cell.fx = inFmt.x(); cell.fy = inFmt.y();
            cell.fr = inFmt.r(); cell.ft = inFmt.t();
            cell.x = std::max(fmt.x(), int(std::floor(ox)));
            cell.y = std::max(fmt.y(), int(std::floor(oy)));
            cell.r = std::min(fmt.r(), int(std::ceil(ox + inFmt.w() * sx)));
            cell.t = std::min(fmt.t(), int(std::ceil(oy + inFmt.h() * sy)));
            _cells.push_back(cell);
        }
    }

    FormatPair  _formats;
    ChannelSet  _channels       = Mask_All;
    bool        _autoLayout     = true;
    int         _rows           = 3;
    int         _columns        = 4;
    float       _gap            = 0.0f;
    int         _rowOrder       = kTopBottom;
    int         _colOrder       = kLeftRight;
    bool        _preserveAspect = true;

    std::vector<Cell> _cells;
};

static Iop* build(Node* node) { return new ContactSheetAll(node); }
const Iop::Description ContactSheetAll::description(kClass, build);