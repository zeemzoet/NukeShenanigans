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
        const int x = box_it.x;
        const int y = box_it.y;

        DeepPixel input_pixel = input_plane.getPixel(y, x);
        size_t input_samples = input_pixel.getSampleCount();

        output_plane.setSampleCount(y, x, input_samples);
        DeepOutputPixel output_pixel = output_plane.getPixel(y, x);

        float previous_front_sample = 0.0f;
        for (size_t sample = 0; sample < input_samples; ++sample) {
            int next_sample = sample - 1;
            if (next_sample >= 0) {
                previous_front_sample = input_pixel.getOrderedSample(next_sample, Chan_DeepFront);
            }
            else {
                previous_front_sample = input_pixel.getOrderedSample(sample, Chan_DeepBack);
            }
            foreach(channel, channels) {
                float& out_data = output_pixel.getWritableOrderedSample(sample, channel);
                const float& in_data = input_pixel.getOrderedSample(sample, channel);
                if (channel == Chan_DeepBack) {
                    out_data = previous_front_sample;
                }
                else {
                    out_data = in_data;
                }
            }
        }
    }

    out = static_cast<DeepOutputPlane>(output_plane);

    return true;
}