//
// Created by Hannes Sap on 01/04/2026.
//
#include "DDImage/Iop.h"
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"

using namespace DD::Image;

static const char* CLASS = "MirrorExtend";
static const char* HELP = "Extends the image by mirroring outside the bbox";

class MirrorExtend : public Iop {

    int width, height;
    float extend_;

    void _validate(bool) override;
    void knobs(Knob_Callback) override;
    //void _request(int, int, int, int, ChannelMask, int) override;
    void engine(int, int, int, ChannelMask, Row&) override;

public:
    explicit MirrorExtend(Node* node) : Iop(node), width(0), height(0), extend_(0.1f) {}

    [[nodiscard]] const char* Class() const override { return CLASS; }
    [[nodiscard]] const char* node_help() const override { return HELP; }
    static const Description description;
};

void MirrorExtend::_validate(bool for_real) {
    copy_info();

    width  = format().width();
    height = format().height();

    const int extended_width = static_cast<int>(std::round(width * extend_));
    const int extended_height = static_cast<int>(std::round(height * extend_));

    info_.set(
        -extended_width,
        -extended_height,
        width + extended_width,
        height + extended_height
    );
}

void MirrorExtend::knobs(Knob_Callback callback) {
    Float_knob(callback, &extend_, "Extend");
}

void MirrorExtend::engine(int y, int x, int r, ChannelMask channels, Row& row) {
    Row input_pixels(0, width);

    // Find the mirrored y if needed
    if (y < 0)
        y = -y;
    else if (y >= height)
        y = height - (y-height) -1;

    input0().get(y, 0, width, channels, input_pixels);
    foreach(z, channels) {
        float* outptr = row.writable(z) + x;
        for (int i = x; i < r; i++) {
            if (i >= 0 && i < width) {
                *outptr++ = input_pixels[z][i];
            }
            else if (i < 0) {
                *outptr++ = input_pixels[z][-i];
            }
            else {
                *outptr++ = input_pixels[z][width - (i-width) -1];
            }
        }
    }
}

static Iop* build_node(Node* node) { return new MirrorExtend(node); }
const Iop::Description MirrorExtend::description(CLASS, build_node);