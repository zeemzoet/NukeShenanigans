//
// Created by Hannes Sap on 09/05/2026.
//
#include <DDImage/DeepFilterOp.h>
#include <DDImage/DeepSample.h>
#include <DDImage/Knobs.h>
#include <vector>

using namespace DD::Image;

static const char* CLASS = "DeepSampleMerge";
static const char* HELP = "Merge unnecessary deep samples";

struct SampleRange {
    uint16_t first, last;

    SampleRange(const int first, const int last) : first(first), last(last) {}
};

struct DeepPixelBuffer {
    size_t input_channel_size {0};
    size_t output_channel_size {0};
    const float* input_data[Chan_Last] = {nullptr};
    float* output_data[Chan_Last] = {nullptr};
};

class DeepSampleMerge : public DeepFilterOp {

    bool _volumise { true };
    bool _merge_opaque { true };
    bool _use_distance_scaling { true };
    float _distance_threshold {0.05f};
    float _alpha_threshold {0.125f};
    Channel diagnostic_channel;

    void merge_samples(const DeepPixelBuffer&, size_t, ChannelMap&, size_t, size_t) const;
    [[nodiscard]] bool merge_error(const DeepPixelBuffer&, size_t, size_t) const;

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
    Bool_knob(f, &_use_distance_scaling, "use_distance_scaling", "Scale Merge Distance by Depth");
    Tooltip(f, "Increases the merge distance for samples further away, "
               "allowing more aggressive merging at greater depths.");
    SetFlags(f, Knob::STARTLINE);
    Float_knob(f, &_distance_threshold, "dist_threshold", "Sample Merge Distance");
    Tooltip(f, "Maximum distance between deep samples before they are merged together.");
    SetFlags(f, Knob::LOG_SLIDER);
    Float_knob(f, &_alpha_threshold, "alpha_threshold", "Alpha Merge Limit");
    Tooltip(f, "Samples with alpha values above this threshold stop further merging, "
               "helping preserve sharp opacity transitions.");
    BeginClosedGroup(f, "Advanced");
    SetFlags(f, Knob::STARTLINE);
    Bool_knob(f, &_volumise, "volumise", "Volumise Samples");
    Tooltip(f, "Gives samples depth by extending their back position, "
               "making them behave like volumetric data rather than flat surfaces.");
    SetFlags(f, Knob::STARTLINE);
    Bool_knob(f, &_merge_opaque, "merge_opaque", "Collapse Hidden Samples");
    Tooltip(f, "Removes samples that are completely hidden behind opaque surfaces, "
               "reducing unnecessary deep data.");
    Channel_knob(f, &diagnostic_channel, 1, "diagnostic_channel");
    Tooltip(f, "Outputs merge information to the selected channel, "
               "showing how many source samples contribute to each result.");
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

    DeepPixelBuffer buffer;
    buffer.input_channel_size = input_plane.channels().size();
    buffer.output_channel_size = output_plane.channels().size();

    std::vector<SampleRange> samples_to_merge;

    for (Box::iterator box_it = box.begin(); box_it != box.end(); ++box_it) {
        DeepPixel input_pixel = input_plane.getPixel(box_it);
        const auto sample_count = static_cast<int>(input_pixel.getSampleCount());

        if (sample_count == 0) {
            output_plane.setSampleCount(box_it, 0);
            continue;
        }

        const float* in_array  = input_pixel.data();

        const ChannelMap& input_channel_map = input_plane.channels();
        foreach(z, channels) {
            if (input_channel_map.contains(z))
                buffer.input_data[z]  = in_array  + input_channel_map.chanNo(z);
        }

        const float* a = buffer.input_data[Chan_Alpha];
        if(!a) continue;

        samples_to_merge.clear();
        samples_to_merge.reserve(sample_count);

        float accumulated_transparency = 1.0f;
        for (int sample = sample_count - 1; sample >= 0; --sample) {
            const float alpha = a[sample * buffer.input_channel_size];
            accumulated_transparency *= (1.0f - alpha);

            const bool extend_last_pair = !samples_to_merge.empty()
                                        && merge_error(buffer, sample, sample + 1);

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
        output_plane.setSampleCount(box_it, samples_to_merge.size());
        DeepOutputPixel output_pixel = output_plane.getPixel(box_it);

        float* out_array = output_pixel.writable();
        ChannelMap output_channel_map = output_plane.channels();
        foreach(z, output_channels) {
            if (output_channel_map.contains(z))
                buffer.output_data[z] = out_array + output_channel_map.chanNo(z);
        }
        size_t out_sample_index = 0;
        for (const auto& [lower, upper] : samples_to_merge) {
            assert(upper < sample_count);
            merge_samples(buffer, out_sample_index, output_channel_map, lower, upper);
            ++out_sample_index;
        }
    }

    output_plane.reviseSamples();
    out = static_cast<DeepOutputPlane>(output_plane);

    return true;
}

bool DeepSampleMerge::merge_error(const DeepPixelBuffer& buffer, size_t lower_index, size_t upper_index) const {
    assert(lower_index <= upper_index);

    assert(buffer.input_data[Chan_DeepFront]);
    assert(buffer.input_data[Chan_DeepBack]);
    assert(buffer.input_data[Chan_Alpha]);

    const size_t lower = lower_index * buffer.input_channel_size;
    const size_t upper = upper_index * buffer.input_channel_size;

    const float front_lower = buffer.input_data[Chan_DeepFront][lower];
    const float front_upper = buffer.input_data[Chan_DeepFront][upper];
    const float back_lower = buffer.input_data[Chan_DeepBack][lower];
    const float back_upper = buffer.input_data[Chan_DeepBack][upper];

    const float alpha_lower = buffer.input_data[Chan_Alpha][lower];
    const float alpha_upper = buffer.input_data[Chan_Alpha][upper];

    const float distance_between = std::abs((front_lower+back_lower) - (front_upper+back_upper)) / 2;
    const float average_depth = (front_lower + back_lower + front_upper + back_upper) / 4;

    const bool alpha_threshold_met = (alpha_lower + alpha_upper) / 2 < _alpha_threshold;
    const bool distance_threshold_met = distance_between < (
        _use_distance_scaling
        ? average_depth * _distance_threshold
        : _distance_threshold
    );

    return alpha_threshold_met && distance_threshold_met;
}

void DeepSampleMerge::merge_samples(const DeepPixelBuffer& buffer, const size_t index, ChannelMap& channels, const size_t lower_index, const size_t upper_index) const {
    assert(lower_index <= upper_index);

    ChannelSet other_channels(channels);
    other_channels -= Chan_Alpha;
    other_channels -= Chan_DeepFront;
    other_channels -= Chan_DeepBack;
    other_channels -= diagnostic_channel;

    const size_t out_index = index * buffer.output_channel_size;
    float total_transparency = 1.0f;  //transparency = (1.0 - alpha)
    for(size_t s = lower_index; s <= upper_index; ++s) {
        const float transparency = 1.0f - buffer.input_data[Chan_Alpha][s * buffer.input_channel_size];
        foreach(z, other_channels) {
            const float* src = buffer.input_data[z];
            float*       dst = buffer.output_data[z];
            dst[out_index] += src[s * buffer.input_channel_size] * total_transparency;
        }
        total_transparency *= transparency;
    }
    buffer.output_data[Chan_Alpha][out_index] = 1.0f - total_transparency;
    if (diagnostic_channel != Chan_Black)
        buffer.output_data[diagnostic_channel][out_index] = static_cast<float>(upper_index-lower_index);

    buffer.output_data[Chan_DeepFront][out_index] = buffer.input_data[Chan_DeepFront][upper_index * buffer.input_channel_size];
    buffer.output_data[Chan_DeepBack][out_index] = _volumise
                                                ? buffer.input_data[Chan_DeepBack][lower_index * buffer.input_channel_size]
                                                : buffer.input_data[Chan_DeepFront][upper_index * buffer.input_channel_size];
}