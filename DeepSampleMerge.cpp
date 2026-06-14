//
// Created by Hannes Sap on 09/05/2026.
//
#include <DDImage/DeepFilterOp.h>
#include <DDImage/DeepSample.h>
#include <DDImage/Knobs.h>
#include <vector>

#define REVERSE 1

using namespace DD::Image;

static const char* CLASS = "DeepSampleMerge";
static const char* HELP = "Merge unnecessary deep samples";

class DeepSampleMerge : public DeepFilterOp {

    bool _volumise { false };
    bool _merge_opaque { true };
    bool _use_distance_scaling { true };
    float _threshold {0.002f};
    Channel diagnostic_channel;

    void merge_samples(DeepOutputPixel& , size_t, const DeepPixel&, size_t, size_t) const;
    [[nodiscard]] bool merge_error(const DeepPixel&, size_t, size_t) const;

public:
    explicit DeepSampleMerge(Node* node) : DeepFilterOp(node) {
        diagnostic_channel = getChannel("diagnostic.value");
    }

    void _validate(bool) override;
    void knobs(Knob_Callback) override;
    bool doDeepEngine(Box, const ChannelSet&, DeepOutputPlane&) override;

    static const Op::Description description;
    [[nodiscard]] const char* Class() const override { return CLASS; }
    [[nodiscard]] const char* node_help() const override { return HELP; }
};

static Op* build(Node* node) { return new DeepSampleMerge(node); }
const Op::Description DeepSampleMerge::description(CLASS, build);

void DeepSampleMerge::_validate(bool for_real) {
    DeepFilterOp::_validate(for_real);
    ChannelSet new_channels = _deepInfo.channels();
    new_channels += diagnostic_channel;
    _deepInfo = DeepInfo(
        _deepInfo.formats(),
        _deepInfo.box(),
        new_channels
    );
}

void DeepSampleMerge::knobs(Knob_Callback f) {
    Bool_knob(f, &_use_distance_scaling, "use_distance_scaling", "Scale Threshold by Distance");
    SetFlags(f, Knob::STARTLINE);
    Float_knob(f, &_threshold, "Threshold");
    BeginClosedGroup(f, "Advanced");
    SetFlags(f, Knob::STARTLINE);
    Bool_knob(f, &_volumise, "volumise", "Volumise");
    SetFlags(f, Knob::STARTLINE);
    Bool_knob(f, &_merge_opaque, "merge_opaque", "Collapse Hidden Samples");
    Channel_knob(f, &diagnostic_channel, 1, "diagnostic_channel");
    EndGroup(f);
}

bool DeepSampleMerge::doDeepEngine(Box box, const ChannelSet& channels, DeepOutputPlane& out) {
    if (!input0())
        return true;

    DeepOp* input_deep = input0();
    DeepPlane input_plane;

    ChannelSet needed_channels = channels;
    needed_channels += Mask_Alpha;
    needed_channels += Mask_Deep;

    if (!input_deep->deepEngine(box, needed_channels, input_plane))
        return false;

    ChannelSet output_channels = channels;
    output_channels += diagnostic_channel;

    DeepInPlaceOutputPlane output_plane(output_channels, box);
    output_plane.reserveSamples(input_plane.getTotalSampleCount());

    std::vector<std::pair<int, int>> samples_to_merge;

    for (Box::iterator box_it = box.begin(); box_it != box.end(); ++box_it) {
        DeepPixel input_pixel = input_plane.getPixel(box_it);
        const size_t sample_count = input_pixel.getSampleCount();

        if (sample_count == 0) {
            output_plane.setSampleCount(box_it, 0);
            continue;
        }

        samples_to_merge.clear();
        samples_to_merge.reserve(sample_count);

        float accumulated_transparency = 1.0f;
#if REVERSE
        for (int sample = sample_count - 1; sample >= 0; --sample) {
            float alpha = input_pixel.getOrderedSample(sample, Chan_Alpha);
            accumulated_transparency *= (1.0f - alpha);

            const bool extend_last_pair = !samples_to_merge.empty()
                                        && merge_error(input_pixel, sample, sample + 1);

            const bool merge_when_opaque = !samples_to_merge.empty()
                                           && _merge_opaque
                                           && accumulated_transparency < 1e-6;

            if (merge_when_opaque) {
                samples_to_merge.back().first = 0;
                break;
            }
            if (extend_last_pair)
                samples_to_merge.back().first = sample;
            else
                samples_to_merge.emplace_back(sample, sample);
        }

#else
        for (int sample = 0; sample < sample_count; ++sample) {
            float alpha = input_pixel.getOrderedSample(sample, Chan_Alpha);
            accumulated_transparency *= (1.0f - alpha);

            const bool extend_last_pair = !samples_to_merge.empty()
                                        && merge_error(input_pixel, sample, sample + 1);

            const bool merge_when_opaque = !samples_to_merge.empty()
                                           && _merge_opaque
                                           && accumulated_transparency < 1e-6;

            if (merge_when_opaque) {
                samples_to_merge.back().second = static_cast<int>(sample_count)-1;
                break;
            }
            if (extend_last_pair)
                samples_to_merge.back().second = sample;
            else
                samples_to_merge.emplace_back(sample, sample);
        }
#endif
        output_plane.setSampleCount(box_it, samples_to_merge.size());
        DeepOutputPixel output_pixel = output_plane.getPixel(box_it);

        size_t out_sample_index = 0;
        for (const auto& [lower, upper] : samples_to_merge) {
            assert(lower >= 0);
            assert(upper < sample_count);
            merge_samples(output_pixel, out_sample_index, input_pixel, lower, upper);
            ++out_sample_index;
        }
    }

    output_plane.reviseSamples();
    out = static_cast<DeepOutputPlane>(output_plane);

    return true;
}

bool DeepSampleMerge::merge_error(const DeepPixel& deep_pixel, size_t lower_index, size_t upper_index) const {
    const size_t sampleCount = deep_pixel.getSampleCount();
    assert(sampleCount > 0);
    assert(lower_index <= upper_index);

    upper_index = std::min(upper_index, sampleCount - 1);
    lower_index = std::min(lower_index, upper_index);

    const float front_lower = deep_pixel.getOrderedSample(lower_index, Chan_DeepFront);
    const float front_upper = deep_pixel.getOrderedSample(upper_index, Chan_DeepFront);

    const float distance_between = front_lower - front_upper;
    const float average_depth = (front_lower + front_upper) / 2.0f;

    if (_use_distance_scaling)
        return distance_between < average_depth * _threshold;

    return distance_between < _threshold;
}


void DeepSampleMerge::merge_samples(
    DeepOutputPixel& output_pixel,
    const size_t out_index,
    const DeepPixel& input_pixel,
    const size_t lower_index,
    const size_t upper_index)
const {

    const size_t sampleCount = input_pixel.getSampleCount();
    assert(sampleCount > 0);
    assert(lower_index <= upper_index);

    ChannelSet other_channels(output_pixel.channels());
    other_channels -= Chan_Alpha;
    other_channels -= Chan_DeepFront;
    other_channels -= Chan_DeepBack;

    float total_transparency = 1.0f;  //transparency = (1.0 - alpha)
    for (auto i = lower_index; i <= upper_index; ++i) {
        const float transparency = 1.0f - input_pixel.getOrderedSample(i, Chan_Alpha);
        foreach(channel, other_channels) {
            const float value = input_pixel.getOrderedSample(i, channel);
            float& out_data = output_pixel.getWritableOrderedSample(out_index, channel);
            out_data = transparency * out_data + value;
        }
        total_transparency *= transparency;
    }

    output_pixel.getWritableOrderedSample(out_index, Chan_Alpha) = 1.0f - total_transparency;
    if (diagnostic_channel != Chan_Black)
        output_pixel.getWritableOrderedSample(out_index, diagnostic_channel) = static_cast<float>(upper_index-lower_index);
    output_pixel.getWritableOrderedSample(out_index, Chan_DeepFront) = input_pixel.getOrderedSample(upper_index, Chan_DeepFront);
    float& out_deep_back_data = output_pixel.getWritableOrderedSample(out_index, Chan_DeepBack);
    if (_volumise)
        out_deep_back_data = input_pixel.getOrderedSample(lower_index, Chan_DeepBack);
    else
        out_deep_back_data = input_pixel.getOrderedSample(upper_index, Chan_DeepFront);
}