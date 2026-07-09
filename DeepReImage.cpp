//
// Created by Hannes Sap on 16/06/2026.
//
#include <complex>
#include <DDImage/DeepFilterOp.h>
#include <DDImage/Iop.h>
#include <DDImage/Knobs.h>
#include <DDImage/Row.h>
#include <chrono>

#define TIMINGS false

using namespace DD::Image;

static const char* CLASS = "DeepReImage";
static const char* HELP = "Bind 2D and Deep data together";

static const char* const bbox_names[] = {
    "union", "image", "deep", nullptr
};

class DeepReImage : public DeepOp, public Iop {

    bool _combine;
    bool _remap_alpha;
    ChannelSet _channels;

    enum {UNION, IMAGE, DEEP};
    int _bbox_type;

    static ChannelSet get_deep_channels(const ChannelSet& channels) {
        ChannelSet new_channels(channels);
        new_channels += Mask_Deep;
        new_channels += Mask_Alpha;
        return new_channels;
    }
    [[nodiscard]] ChannelSet get_colour_channels(const ChannelSet& channels, const bool with_alpha=false) const {
        ChannelSet new_channels(channels);
        new_channels &= _channels;
        if (!_remap_alpha && !with_alpha)
            new_channels -= Mask_Alpha;
        new_channels -= Mask_Deep;
        return new_channels;
    }

    [[nodiscard]] Iop* image_input() const { return dynamic_cast<Iop*>(Op::input(0)); }
    [[nodiscard]] DeepOp* deep_input() const { return dynamic_cast<DeepOp*>(Op::input(1)); }

public:
    explicit DeepReImage(Node* node) : Iop(node)
    , _combine(true)
    , _remap_alpha(true)
    , _channels(Mask_All)
    , _bbox_type(UNION)
    {}

    // Deep stuff
    Op* op() override { return this; }
    void _validate(bool) override;
    void getDeepRequests(Box, const ChannelSet&, int, std::vector<RequestData>&) override;
    bool doDeepEngine(Box, const ChannelSet&, DeepOutputPlane&) override;

    // Image stuff
    void _request(int, int, int, int, ChannelMask, int count) override;
    void engine(int, int, int, ChannelMask, Row&) override;

    void knobs(Knob_Callback) override;
    void append(Hash&) override;

    [[nodiscard]] int minimum_inputs() const override { return 2; }
    [[nodiscard]] int maximum_inputs() const override{ return 2; }
    const char* input_label(int, char*) const override;
    bool test_input(int, Op*) const override;
    [[nodiscard]] const char* node_shape() const override { return "/)"; }

    static const Description description;
    [[nodiscard]] const char* Class() const override { return CLASS; }
    [[nodiscard]] const char* node_help() const override { return HELP; }
};

static Iop* build(Node* node) { return new DeepReImage(node); }
void DeepReImage::_validate(bool for_real) {
    Box total_box = Box();
    ChannelSet total_channels(Mask_None);
    FormatPair total_formats = FormatPair();
    if (const auto image = image_input()) {
        image->validate(for_real);
        const auto& info = image->info();

        switch (_bbox_type) {
            case UNION:
                total_box.merge(info.box());
                break;
            case IMAGE:
                total_box = info.box();
                break;
            default: ;
        }
        total_formats = info.formats();
        total_channels += info.channels();
    }
    if (const auto deep = deep_input()) {
        deep->validate(for_real);
        const auto& deepinfo = deep->deepInfo();
        switch (_bbox_type) {
            case UNION:
                total_box.merge(deepinfo.box());
                break;
            case DEEP:
                total_box = deepinfo.box();
                break;
            default: ;
        }
        total_formats = deepinfo.formats();
        total_channels += get_deep_channels(deepinfo.channels());
    }
    info_.set(total_box);
    info_.setFormats(total_formats);
    total_channels &= _channels;
    info_.channels() = total_channels;
    _deepInfo = DeepInfo(info_);
}

void DeepReImage::getDeepRequests(Box box, const ChannelSet& channels, int count, std::vector<RequestData>& requests) {
    if (const auto image = image_input())
        image->request(box, get_colour_channels(channels), count);
    if (const auto deep = deep_input())
        requests.emplace_back(deep, box, get_deep_channels(channels), count);
}

bool DeepReImage::doDeepEngine(Box box, const ChannelSet& channels, DeepOutputPlane& out) {
    if (!deep_input())
        return false;

    if (!_combine) {
        if (!deep_input()->deepEngine(box, channels, out))
            return false;
        return true;
    }

    DeepPlane input_plane;
    if (!deep_input()->deepEngine(box, get_deep_channels(channels), input_plane))
        return false;

    DeepInPlaceOutputPlane output_plane(channels, box);

    // precompute chanNo lookups once — outside all loops
    const size_t input_channel_size = input_plane.channels().size();
    const size_t output_channel_size = channels.size();

    // --- pointer table: input_data[z] points to sample 0 of channel z ---
    // one pointer per channel enum slot, stack allocated
    const float* input_data[Chan_Last] = {nullptr};
    float* output_data[Chan_Last] = {nullptr};

    std::vector<float> alpha_samples;
    alpha_samples.reserve(512);  //hoping for the best and not too many samples

    // iterate over y
    for (int y = box.y(); y < box.t(); ++y) {

        Row input_row(box.x(), box.r());
        image_input()->get(y, box.x(), box.r(), get_colour_channels(channels), input_row);

#if TIMINGS
        auto t0 = std::chrono::high_resolution_clock::now();
#endif

        // iterate over x
        for (int x = box.x(); x < box.r(); ++x) {

            DeepPixel input_pixel = input_plane.getPixel(y, x);
            const size_t sample_count = input_pixel.getSampleCount();

            output_plane.setSampleCount(y, x, sample_count);
            DeepOutputPixel output_pixel = output_plane.getPixel(y,x);

            const float* in_array  = input_pixel.data();
            float*       out_array = output_pixel.writable();

            const ChannelMap& input_channel_map = input_plane.channels();
            const ChannelMap& output_channel_map = output_plane.channels();
            foreach(z, channels) {
                if (input_channel_map.contains(z))
                    input_data[z]  = in_array  + input_channel_map.chanNo(z);   // sample s is input_data[z][s * channel_size]
                if (output_channel_map.contains(z))
                    output_data[z] = out_array + output_channel_map.chanNo(z);
            }

            if (sample_count == 0)
                continue;

            const float* a = input_data[Chan_Alpha];
            if (!a) continue;

            // fingers crossed we don't often hit 512 samples.
            if (alpha_samples.size() < sample_count)
                alpha_samples.resize(sample_count);

            float total_transparency = 1.0f;
            for (size_t s = 0; s < sample_count; ++s) {
                float alpha = a[s * input_channel_size];
                total_transparency *= (1.0f - alpha);
                alpha_samples[s] = alpha;
            }

            const float image_alpha = _remap_alpha ? input_row[Chan_Alpha][x] : 1.0f;
            const float alpha_scale = image_alpha / (1.0f - total_transparency);
            if (_remap_alpha) {
                float input_transparency = 1.0f;
                float output_transparency = 1.0f;
                for (size_t s = 0; s < sample_count; ++s) {
                    input_transparency *= 1.0f - alpha_samples[s];

                    float target_transparency = 1.0f - (1.0f - input_transparency) * alpha_scale;
                    if (output_transparency > 1e-6f)
                        alpha_samples[s] = 1.0f - target_transparency / output_transparency;
                    else
                        alpha_samples[s] = 1.0f;
                    output_transparency = target_transparency;
                }
            }

            foreach(z, ChannelSet(Mask_Deep)) {
                const float* src = input_data[z];
                float*       dst = output_data[z];
                for (size_t s = 0; s < sample_count; ++s) {
                    float value = src ? src[s * input_channel_size] : 0.0f;
                    dst[s * output_channel_size] = value;
                }
            }

            for (size_t s = 0; s < sample_count; ++s)
                output_data[Chan_Alpha][s * output_channel_size] = alpha_samples[s];

            foreach(z, get_colour_channels(channels)) {
                const float row_val = input_row[z][x];
                float*      dst     = output_data[z];
                const float scaled_row_val = _remap_alpha ? row_val / image_alpha : row_val;
                for (size_t s = 0; s < sample_count; ++s)
                    dst[s * output_channel_size] = scaled_row_val * alpha_samples[s];
            }
        }
#if TIMINGS
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        printf("%f\n", ms);
#endif
    }

    out = static_cast<DeepOutputPlane>(output_plane);
    return true;
}

void DeepReImage::_request(int x, int y, int r, int t, ChannelMask channels, int count) {
    if (const auto image = image_input())
        image->request(x, y, r, t, get_colour_channels(channels, true), count);
    if (const auto deep = deep_input())
        deep->deepRequest(Box(x,y,r,t), get_deep_channels(channels), count);
}

void DeepReImage::engine(int y, int x, int r, ChannelMask channels, Row& out_row) {
    // If we're not combining deep and image,
    // pass through the image straight away
    if (!_combine || !deep_input()) {
        image_input()->engine(y, x, r, channels, out_row);
        return;
    }

    if (!image_input()) {
        foreach(z, channels)
            out_row.erase(z);
        return;
    }

    DeepPlane input_plane;
    if (!deep_input()->deepEngine(y, x, r, get_deep_channels(channels), input_plane)) {
        abort();
        foreach(z, channels) {
            out_row.erase(z);
        }
        return;
    }

    Row in_row(x, r);
    const ChannelSet colour_channels = get_colour_channels(channels, true);
    const ChannelSet other_channels = channels - colour_channels;

    image_input()->get(y, x, r, colour_channels, in_row);

    std::vector<float> deep_alpha(r - x);
    float total_transparency = 1.0f;
    for (int i = x; i < r; i++) {
        auto deep_pixel = input_plane.getPixel(y, i);
        for (size_t s = 0; s < deep_pixel.getSampleCount(); ++s) {
            total_transparency *= (1.0f - deep_pixel.getUnorderedSample(s, Chan_Alpha));
        }
        deep_alpha[i - x] = 1.0f - total_transparency;
    }
    foreach(z, colour_channels) {
        float* out_ptr = out_row.writable(z);
        const float* in_ptr = in_row[z];
        for (int i = x; i < r; i++) {
            if (z == Chan_Alpha && !_remap_alpha) {
                out_ptr[i] = deep_alpha[i - x];
                continue;
            }
            out_ptr[i] = deep_alpha[i - x] * in_ptr[i];
        }
    }
    foreach(z, other_channels) {
        float* out_ptr = out_row.writable(z);
        for (int i = x; i < r; i++) {
            auto deep_pixel = input_plane.getPixel(y, i);
            size_t sample_count = deep_pixel.getSampleCount();
            if (sample_count == 0) {
                out_ptr[i] = 0.0f;
                continue;
            }
            for (size_t s = 0; s < sample_count; ++s)
                out_ptr[i] = deep_pixel.getUnorderedSample(s, z);
        }
    }
}

void DeepReImage::knobs(Knob_Callback f) {
    Input_ChannelMask_knob(f, &_channels, 0, "channels");
    SetFlags(f, Knob::STARTLINE);
    Bool_knob(f, &_combine, "combine_deep", "Combine");
    Bool_knob(f, &_remap_alpha, "use_image_alpha", "Use Image Alpha");
    Enumeration_knob(f, &_bbox_type, bbox_names, "bbox", "Set BBox to");
}

void DeepReImage::append(Hash& hash) {
    hash.append(_channels);
    hash.append(_combine);
    hash.append(_remap_alpha);
    hash.append(_bbox_type);
}

const char* DeepReImage::input_label(int input, char* buffer) const {
    switch (input) {
        case 0:
            return "colour";
        case 1:
            return "deep";
        default:
            return nullptr;
    }
}

bool DeepReImage::test_input(int input, Op* op) const {
    switch (input) {
        case 0:
            return dynamic_cast<Iop*>(op) != nullptr;
        case 1:
            return dynamic_cast<DeepOp*>(op) != nullptr;
        default:
            return false;
    }
}

#if DD_IMAGE_VERSION_MAJOR >= 17
const Op::Description DeepReImage::description(CLASS, build);
#else
const Op::Description DeepReImage::description(CLASS, nullptr, build);
#endif
