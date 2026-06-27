//
// Created by Hannes Sap on 24/06/2026.
//
#include <DDImage/DeepFilterOp.h>
#include <DDImage/DeepComposite.h>
#include <DDImage/DeepSample.h>
#include <DDImage/DeepAccumPixelOp.h>

using namespace DD::Image;

static const char* CLASS = "DeepKeymix";
static const char* HELP = "Combines 2 Deeps based on a matte";

class DeepKeymix : public DeepOnlyOp {

    [[nodiscard]] DeepOp* b_input() const { return dynamic_cast<DeepOp*>(input(0)); }
    [[nodiscard]] DeepOp* a_input() const { return dynamic_cast<DeepOp*>(input(1)); }

    static void scale_deep_sample(DeepSample&, const ChannelSet&, float);

public:
    explicit DeepKeymix(Node* node) : DeepOnlyOp(node) {}

    void _validate(bool) override;
    void getDeepRequests(Box, const ChannelSet&, int, std::vector<RequestData>&) override;
    bool doDeepEngine(Box, const ChannelSet&, DeepOutputPlane&) override;

    [[nodiscard]] int minimum_inputs() const override { return 3; }
    [[nodiscard]] int maximum_inputs() const override{ return 3; }
    const char* input_label(int, char*) const override;
    bool test_input(int, Op*) const override;

    static const Description description;
    [[nodiscard]] const char* Class() const override { return CLASS; }
    [[nodiscard]] const char* node_help() const override { return HELP; }
};

static Op* build(Node* node) { return new DeepKeymix(node); }
const Op::Description DeepKeymix::description(CLASS, build);

const char* DeepKeymix::input_label(int input, char * buffer) const {
    switch (input) {
        case 0: return "B";
        case 1: return "A";
        case 2: return "mask";
        default: return nullptr;
    }
}

bool DeepKeymix::test_input(int input, Op* op) const {
    switch (input) {
        case 0:
        case 1:
            return dynamic_cast<DeepOp*>(op) != nullptr;
        case 2:
            return dynamic_cast<Iop*>(op) != nullptr;
        default:
            return false;
    }
}

void DeepKeymix::_validate(bool for_real) {
    Box total_box = Box();
    ChannelSet total_channels(Mask_None);
    FormatPair total_formats = FormatPair();
    if (b_input()) {
        b_input()->validate(for_real);
        const auto& deepinfo = b_input()->deepInfo();

        total_box.merge(deepinfo.box());
        total_channels += deepinfo.channels();
        total_formats = deepinfo.formats();
    }
    if (a_input()) {
        a_input()->validate(for_real);
        const auto& deepinfo = a_input()->deepInfo();

        total_box.merge(deepinfo.box());
        total_channels += deepinfo.channels();
    }
    _deepInfo = DeepInfo(total_formats, total_box, total_channels);
}

void DeepKeymix::getDeepRequests(Box box, const ChannelSet& channels, int count, std::vector<RequestData>& requests) {
    if (b_input())
        b_input()->deepRequest(box, channels, count);
    if (a_input())
        a_input()->deepRequest(box, channels, count);
}

bool DeepKeymix::doDeepEngine(Box box, const ChannelSet& channels, DeepOutputPlane& out) {
    if (!b_input() || !a_input())
        return false;

    DeepPlane b_input_plane;
    DeepPlane a_input_plane;
    ChannelSet needed_channels = channels;
    needed_channels += Mask_Alpha;
    needed_channels += Mask_Deep;

    if (!b_input()->deepEngine(box, needed_channels, b_input_plane))
        return false;
    if (!a_input()->deepEngine(box, needed_channels, a_input_plane))
        return false;


    DeepInPlaceOutputPlane output_plane(channels, box);
    output_plane.reserveSamples(b_input_plane.getTotalSampleCount() + a_input_plane.getTotalSampleCount());

    for (Box::iterator box_it = box.begin(); box_it != box.end(); ++box_it) {
        DeepPixel b_input_pixel = b_input_plane.getPixel(box_it);
        DeepPixel a_input_pixel = a_input_plane.getPixel(box_it);

        DeepSampleVector total_incoming;
        DeepSampleVector total_outgoing;
        ChannelMap all_channels(channels);
        ChannelSet other_channels(channels);
        other_channels -= Mask_Alpha;
        other_channels -= Mask_Deep;

        float input_transparency = 1.0f;
        float output_transparency = 1.0f;
        for (size_t s = 0; s < b_input_pixel.getSampleCount(); ++s) {
            DeepSample b_sample(all_channels, b_input_pixel, s);

            float b_alpha = b_sample[Chan_Alpha];
            input_transparency *= 1.0f - b_sample[Chan_Alpha];
            float target_transparency = 1.0f - (1.0f - input_transparency) * 0.5;
            if (output_transparency > 1e-6f)
                b_sample[Chan_Alpha] = 1.0f - target_transparency / output_transparency;
            else
                b_sample[Chan_Alpha] = 1.0f;
            output_transparency = target_transparency;

            scale_deep_sample(b_sample, other_channels, b_alpha);
            total_incoming.push_back(b_sample);
        }

        input_transparency = 1.0f;
        output_transparency = 1.0f;
        for (size_t s = 0; s < a_input_pixel.getSampleCount(); ++s) {
            DeepSample a_sample(all_channels, a_input_pixel, s);

            float a_alpha = a_sample[Chan_Alpha];
            input_transparency *= 1.0f - a_sample[Chan_Alpha];
            float target_transparency = 1.0f - (1.0f - input_transparency) * 0.5;
            if (output_transparency > 1e-6f)
                a_sample[Chan_Alpha] = 1.0f - target_transparency / output_transparency;
            else
                a_sample[Chan_Alpha] = 1.0f;
            output_transparency = target_transparency;

            scale_deep_sample(a_sample, other_channels, a_alpha);
            total_incoming.push_back(a_sample);
        }

        CombineOverlappingSamples(all_channels, total_incoming, total_outgoing);

        output_plane.setSampleCount(box_it, total_outgoing.size());
        DeepOutputPixel out_pixel = output_plane.getPixel(box_it);

        if (total_outgoing.size() > 0) {
            foreach(z, channels) {
                size_t index = 0;
                for (DeepSample sample: total_outgoing) {
                    float& outfloat = out_pixel.getWritableUnorderedSample(index, z);
                    float infloat = sample[z];
                    outfloat = infloat;
                    ++index;
                }
            }
        }
    }

    output_plane.reviseSamples();
    out = static_cast<DeepOutputPlane>(output_plane);
    return true;
}

void DeepKeymix::scale_deep_sample(DeepSample& deep_sample, const ChannelSet& channels, const float orig_alpha) {
    foreach(z,channels) {
        deep_sample[z] = deep_sample[z] / orig_alpha * deep_sample[Chan_Alpha];
    }
}
