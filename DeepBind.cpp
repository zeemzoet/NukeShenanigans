//
// Created by Hannes Sap on 16/06/2026.
//
#include <complex>
#include <DDImage/DeepFilterOp.h>
#include <DDImage/Iop.h>
#include <DDImage/Knobs.h>

using namespace DD::Image;

static const char* CLASS = "DeepBind";
static const char* HELP = "Bind 2D and Deep data together";

class DeepBind : public DeepFilterOp {
    ChannelSet _2D_Channels;

public:
    explicit DeepBind(Node* node) : DeepFilterOp(node) {}

    void _validate(bool) override;
    void getDeepRequests(Box, const ChannelSet&, int, std::vector<RequestData>&) override;
    //void knobs(Knob_Callback) override;
    bool doDeepEngine(Box, const ChannelSet&, DeepOutputPlane&) override;

    [[nodiscard]] int minimum_inputs() const override { return 2; }
    [[nodiscard]] int maximum_inputs() const override{ return 2; }
    const char* input_label(int, char*) const override;
    bool test_input(int, Op*) const override;

    [[nodiscard]] Iop* image_input() const { return dynamic_cast<Iop*>(input(0)); }
    [[nodiscard]] DeepOp* deep_input() const { return dynamic_cast<DeepOp*>(input(1)); }

    static const Description description;
    [[nodiscard]] const char* Class() const override { return CLASS; }
    [[nodiscard]] const char* node_help() const override { return HELP; }
};

static Op* build(Node* node) { return new DeepBind(node); }
const Op::Description DeepBind::description(CLASS, build);

const char* DeepBind::input_label(int input, char* buffer) const {
    switch (input) {
        case 0:
            return "colour";
        case 1:
            return "deep";
        default:
            return nullptr;
    }
}

bool DeepBind::test_input(int input, Op* op) const {
    switch (input) {
        case 0:
            return dynamic_cast<Iop*>(op) != nullptr;
        case 1:
            return dynamic_cast<DeepOp*>(op) != nullptr;
        default:
            return false;
    }
}

void DeepBind::_validate(bool for_real) {
    DeepFilterOp::_validate(for_real);
    ChannelSet new_channels = _deepInfo.channels();
    if (image_input()) {
        image_input()->validate(true);
        new_channels += image_input()->info().channels();
    }
    _deepInfo = DeepInfo(
        _deepInfo.formats(),
        _deepInfo.box(),
        new_channels
    );
}

void DeepBind::getDeepRequests(Box box, const ChannelSet& channels, int count, std::vector<RequestData>& requests) {
    image_input()->request(box, channels, count);
}


bool DeepBind::doDeepEngine(Box box, const ChannelSet& channels, DeepOutputPlane& out) {
    if (!deep_input())
        return false;

    DeepPlane input_plane;
    ChannelSet needed_channels = channels;
    needed_channels += Mask_Alpha;
    needed_channels += Mask_Deep;

    if (!deep_input()->deepEngine(box, needed_channels, input_plane))
        return false;

    ChannelSet colour_channels = channels;
    colour_channels -= Mask_Alpha;
    colour_channels -= Mask_Deep;

    DeepInPlaceOutputPlane output_plane(channels, box);
    for (Box::iterator box_it = box.begin(); box_it != box.end(); ++box_it) {
        DeepPixel input_pixel = input_plane.getPixel(box_it);
        const size_t sample_count = input_pixel.getSampleCount();

        output_plane.setSampleCount(box_it, sample_count);
        DeepOutputPixel output_pixel = output_plane.getPixel(box_it);

        float accumulated_transparency = 1.0f;
        for (size_t s = 0; s < sample_count; ++s) {
            float alpha = input_pixel.getOrderedSample(s, Chan_Alpha);
            accumulated_transparency *= (1.0f - alpha);

            foreach(channel, colour_channels) {
                float value = image_input()->at(box_it.x, box_it.y, channel);
                value *= alpha;
                output_pixel.getWritableOrderedSample(s, channel) = value;
            }
            output_pixel.getWritableOrderedSample(s, Chan_Alpha) = alpha;
            foreach(channel, ChannelSet(Mask_Deep)) {
                output_pixel.getWritableOrderedSample(s, channel) =
                    input_pixel.getOrderedSample(s, channel);
            }
        }
    }

    out = static_cast<DeepOutputPlane>(output_plane);

    return true;
}