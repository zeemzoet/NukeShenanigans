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
    float extend_;
    float bbox_[4];

    void _validate(bool) override;
    void knobs(Knob_Callback) override;
    //void _request(int, int, int, int, ChannelMask, int) override;
    void engine(int, int, int, ChannelMask, Row&) override;

public:
    explicit MirrorExtend(Node* node) : Iop(node), extend_(0.1f) {
        bbox_[0] = bbox_[2] = 0.0f;
        bbox_[2] = input_format().width();
        bbox_[3] = input_format().height();
    }

    [[nodiscard]] const char* Class() const override { return CLASS; }
    [[nodiscard]] const char* node_help() const override { return HELP; }
    static const Description description;
};

void MirrorExtend::_validate(bool for_real) {
    copy_info();

    float width  = std::abs(bbox_[2]-bbox_[0]);
    float height = std::abs(bbox_[3]-bbox_[1]);

    const int extended_width = static_cast<int>(std::round(width * extend_));
    const int extended_height = static_cast<int>(std::round(height * extend_));

    info_.set(
        bbox_[0] - extended_width,
        bbox_[1] - extended_height,
        bbox_[2] + extended_width,
        bbox_[3] + extended_height
    );
}

void MirrorExtend::knobs(Knob_Callback callback) {
    BBox_knob(callback, &bbox_[0], "BBox");
    Float_knob(callback, &extend_, "Extend");
}

void MirrorExtend::engine(int y, int x, int r, ChannelMask channels, Row& row) {
    Row input_pixels(bbox_[0], bbox_[2]);

    // Find the mirrored y if needed
    if (y < bbox_[1])
        y = 2*bbox_[1] -y;
    else if (y >= bbox_[3])
        y = 2*bbox_[3] - y -1;  //reflected y

    input0().get(y, bbox_[0], bbox_[2], channels, input_pixels);
    foreach(z, channels) {
        float* outptr = row.writable(z) + x;
        for (int i = x; i < r; i++) {
            if (i >= bbox_[0] && i < bbox_[2]) {
                *outptr++ = input_pixels[z][i];
            }
            else if (i < bbox_[0]) {
                int refl_x = 2*bbox_[0] - i;
                *outptr++ = input_pixels[z][refl_x];
            }
            else {
                int refl_x = 2*bbox_[2] -i -1;
                *outptr++ = input_pixels[z][refl_x];  // reflected x
            }
        }
    }
}

static Iop* build_node(Node* node) { return new MirrorExtend(node); }
const Iop::Description MirrorExtend::description(CLASS, build_node);