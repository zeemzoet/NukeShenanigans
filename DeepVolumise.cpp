//
// Created by Hannes Sap on 05/04/2026.
//
#include "DDImage/Pixel.h"
#include "DDImage/DeepFilterOp.h"

using namespace DD::Image;

static const char* CLASS = "DeepVolumise";
static const char* HELP = "Extending Deep volume samples so front and back values are different";

class DeepVolumise : public DeepFilterOp {

public:
    explicit DeepVolumise(Node* node) : DeepFilterOp(node) {}

    bool doDeepEngine(Box, const ChannelSet&, DeepOutputPlane&) override;

    static const Op::Description description;
    [[nodiscard]] const char* Class() const override { return CLASS; }
    [[nodiscard]] const char* node_help() const override { return HELP; }
};

static Op* build_func(Node* node) { return new DeepVolumise(node); }
const Op::Description DeepVolumise::description(CLASS, build_func);

bool DeepVolumise::doDeepEngine(const Box box, const ChannelSet& channels, DeepOutputPlane& out) {
    if (!input0())
        return true;

    DeepOp* input_deep = input0();
    DeepPlane input_plane;

    ChannelSet needed_channels = channels;
    needed_channels += Mask_DeepFront;

    if (!input_deep->deepEngine(box, needed_channels, input_plane))
        return false;

    DeepInPlaceOutputPlane output_plane(channels, box);
    output_plane.reserveSamples(input_plane.getTotalSampleCount());

    for (Box::iterator box_it = box.begin(); box_it != box.end(); ++box_it) {
        DeepPixel input_pixel = input_plane.getPixel(box_it);
        const size_t sample_count = input_pixel.getSampleCount();

        output_plane.setSampleCount(box_it, sample_count);
        DeepOutputPixel output_pixel = output_plane.getPixel(box_it);

        for (size_t s = 0; s < sample_count; ++s) {
            const bool has_previous = static_cast<int>(s - 1) >= 0;
            const float deep_back = has_previous
                ? input_pixel.getOrderedSample(s - 1, Chan_DeepFront)
                : input_pixel.getOrderedSample(s, Chan_DeepBack); // last sample: leave back unchanged

            foreach(channel, channels) {
                float& out_data = output_pixel.getWritableOrderedSample(s, channel);
                out_data = (channel == Chan_DeepBack)
                    ? deep_back
                    : input_pixel.getOrderedSample(s, channel);
            }
        }
    }

    out = static_cast<DeepOutputPlane>(output_plane);

    return true;
}