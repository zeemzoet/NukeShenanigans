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

static const char* const algorithms_names[] = {
    "Simple", "Fancy (Douglas-Pecker)", nullptr
};

struct SampleRange {
    uint16_t first, last;
    SampleRange(const int first, const int last) : first(first), last(last) {
        assert(first >= 0);
        assert(last >= 0);
        assert(first <= std::numeric_limits<uint16_t>::max());
        assert(last <= std::numeric_limits<uint16_t>::max());
    }
};

struct DeepPlotSample {
    uint16_t sample;
    float x, y;
    DeepPlotSample(const uint16_t s, const float x, const float y) : sample(s), x(x), y(y) {}
};

static void ramer_douglas_peucker(const std::vector<DeepPlotSample>&, int, int, float, std::vector<uint8_t>&);

struct DeepPixelBuffer {
    size_t input_channel_size {0};
    size_t output_channel_size {0};
    const float* input_data[Chan_Last] = {nullptr};
    float* output_data[Chan_Last] = {nullptr};
};

class DeepSampleMerge : public DeepFilterOp {

    enum {SIMPLE, RDP};
    int _algorithm_choice {RDP};

    bool _use_distance_scaling { true };
    float _distance_threshold {0.05f};
    float _alpha_threshold {0.125f};

    float _rdp_epsilon {0.005f};

    bool _volumise { true };
    bool _merge_opaque { true };
    Channel _diagnostic_channel;

    void merge_sample_range(const DeepPixel&, DeepOutputPixel&, size_t, const SampleRange&) const;
    void should_merge_distance_alpha(std::vector<SampleRange>&, const DeepPixel&) const;
    void should_merge_RDP(std::vector<SampleRange>&, const DeepPixel&) const;

public:
    explicit DeepSampleMerge(Node* node) : DeepFilterOp(node) {
        _diagnostic_channel = getChannel("diagnostic.value");
    }

    void _validate(bool) override;
    void knobs(Knob_Callback) override;
    int knob_changed(Knob *) override;
    void append(Hash&) override;
    bool doDeepEngine(Box, const ChannelSet&, DeepOutputPlane&) override;

    static const Op::Description description;
    [[nodiscard]] const char* Class() const override { return CLASS; }
    [[nodiscard]] const char* node_help() const override { return HELP; }
};

static Op* build(Node* node) { return new DeepSampleMerge(node); }

void DeepSampleMerge::merge_sample_range(const DeepPixel &input, DeepOutputPixel& output, const size_t index,
                                    const SampleRange& sample_pair) const {
    assert(sample_pair.first <= sample_pair.last);

    ChannelSet other_channels(output.channels());
    other_channels -= Chan_Alpha;
    other_channels -= Chan_DeepFront;
    other_channels -= Chan_DeepBack;
    other_channels -= _diagnostic_channel;

    float total_transparency = 1.0f;  //transparency = (1.0 - alpha)
    for(size_t s = sample_pair.first; s <= sample_pair.last; ++s) {
        const float alpha = input.getOrderedSample(s, Chan_Alpha);
        const float transparency = 1.0f - alpha;
        foreach(z, other_channels) {
            const float src = input.getOrderedSample(s, z);
            float&      dst = output.getWritableOrderedSample(index, z);
            dst = dst * transparency + src;
        }
        total_transparency *= transparency;
    }
    output.getWritableOrderedSample(index, Chan_Alpha) = 1.0f - total_transparency;
    if (_diagnostic_channel != Chan_Black)
        output.getWritableOrderedSample(index, _diagnostic_channel) = static_cast<float>(sample_pair.last-sample_pair.first);

    output.getWritableOrderedSample(index, Chan_DeepFront) = input.getOrderedSample(sample_pair.last, Chan_DeepFront);
    output.getWritableOrderedSample(index, Chan_DeepBack) = _volumise
                                                        ? input.getOrderedSample(sample_pair.first, Chan_DeepBack)
                                                        : input.getOrderedSample(sample_pair.last, Chan_DeepFront);
}

void DeepSampleMerge::should_merge_distance_alpha(std::vector<SampleRange>& samples_to_merge, const DeepPixel& input_pixel) const {
    auto should_merge = [&](size_t lower, size_t upper) {
        assert(lower <= upper);

        assert(input_pixel.channels().contains(Chan_Alpha));
        assert(input_pixel.channels().contains(Chan_DeepFront));
        assert(input_pixel.channels().contains(Chan_DeepBack));

        const float front_lower = input_pixel.getOrderedSample(lower, Chan_DeepFront);
        const float front_upper = input_pixel.getOrderedSample(upper, Chan_DeepFront);
        const float back_lower = input_pixel.getOrderedSample(lower, Chan_DeepBack);
        const float back_upper = input_pixel.getOrderedSample(upper, Chan_DeepBack);

        const float alpha_lower = input_pixel.getOrderedSample(lower, Chan_Alpha);
        const float alpha_upper = input_pixel.getOrderedSample(upper, Chan_Alpha);

        const float distance_between = std::abs((front_lower+back_lower) - (front_upper+back_upper)) / 2;
        const float average_depth = (front_lower + back_lower + front_upper + back_upper) / 4;

        const bool alpha_threshold_met = (alpha_lower + alpha_upper) / 2 < _alpha_threshold;
        const bool distance_threshold_met = distance_between < (
                                                _use_distance_scaling
                                                    ? average_depth * _distance_threshold
                                                    : _distance_threshold
                                            );

        return alpha_threshold_met && distance_threshold_met;
    };

    float accumulated_transparency = 1.0f;
    auto sample_count = static_cast<int>(input_pixel.getSampleCount());
    for (int sample = sample_count - 1; sample >= 0; --sample) {
        const float alpha = input_pixel.getOrderedSample(sample, Chan_Alpha);
        accumulated_transparency *= (1.0f - alpha);

        const bool extend_last_pair = !samples_to_merge.empty()
                                      && should_merge(sample, sample + 1);

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
}

void DeepSampleMerge::should_merge_RDP(std::vector<SampleRange>& samples_to_merge, const DeepPixel& input) const {
    auto sample_count = static_cast<int>(input.getSampleCount());

    // First we make a graph, plotting the deep samples, remembering which
    // original sample each plotted point came from.
    std::vector<DeepPlotSample> deep_plotted;
    deep_plotted.reserve(sample_count * 2);

    float total_transparency = 1.0f;
    float total_avg_depth = 0.0f;
    for (int sample = sample_count - 1; sample >= 0; --sample) {
    //for (int sample = 0; sample < sample_count; ++sample) {
        float front = input.getOrderedSample(sample, Chan_DeepFront);
        float back  = input.getOrderedSample(sample, Chan_DeepBack);
        const float alpha = input.getOrderedSample(sample, Chan_Alpha);

        total_avg_depth += (front + back) / 2;
        front = _use_distance_scaling ? front / total_avg_depth : front;
        back  = _use_distance_scaling ? back / total_avg_depth : back;

        deep_plotted.emplace_back(sample, front, 1.0f - total_transparency);

        /*if (front != back) {
            deep_plotted.emplace_back(sample, back, 1.0f - total_transparency);
        }*/

        total_transparency *= (1.0f - alpha);
    }

    const int deep_plotted_size = static_cast<int>(deep_plotted.size());
    samples_to_merge.reserve(deep_plotted_size);

    if (deep_plotted_size < 3) {
        for (int i = 0; i < deep_plotted_size; ++i)
            samples_to_merge.emplace_back(deep_plotted[i].sample, deep_plotted[i].sample);
        return;
    }

    std::vector<uint8_t> keep(deep_plotted_size, 0);
    keep[0] = 1;
    keep[deep_plotted_size - 1] = 1;

    ramer_douglas_peucker(deep_plotted, 0, deep_plotted_size - 1, _rdp_epsilon, keep);

    for (size_t i = 0; i < deep_plotted_size; ++i) {
        const uint16_t orig_sample = deep_plotted[i].sample;
        if (keep[i]) {
            samples_to_merge.emplace_back(orig_sample, orig_sample);
        } else {
            SampleRange& current = samples_to_merge.back();
            current.first = std::min(current.first, orig_sample);
            current.last  = std::max(current.last, orig_sample);
        }
    }
}

void DeepSampleMerge::_validate(bool for_real) {
    DeepFilterOp::_validate(for_real);
    ChannelSet new_channels = _deepInfo.channels();
    new_channels += _diagnostic_channel;
    new_channels += Mask_Deep;
    new_channels += Mask_Alpha;
    _deepInfo = DeepInfo(
        _deepInfo.formats(),
        _deepInfo.box(),
        new_channels
    );
}

void DeepSampleMerge::knobs(Knob_Callback f) {
    Enumeration_knob(f, &_algorithm_choice, algorithms_names, "method", "Method");
    Tooltip(f, "Choose what method / algorithm to run to merge deep samples");
    SetFlags(f, Knob::STARTLINE);

    Bool_knob(f, &_use_distance_scaling, "use_distance_scaling", "Scale Merge Distance by Depth");
    Tooltip(f, "Increases the merge distance for samples further away, "
            "allowing more aggressive merging at greater depths.");

    Float_knob(f, &_distance_threshold, "dist_threshold", "Sample Merge Distance");
    Tooltip(f, "Maximum distance between deep samples before they are merged together.");
    SetFlags(f, Knob::LOG_SLIDER);
    Float_knob(f, &_alpha_threshold, "alpha_threshold", "Alpha Merge Limit");
    Tooltip(f, "Samples with alpha values above this threshold stop further merging, "
            "helping preserve sharp opacity transitions.");
    Bool_knob(f, &_merge_opaque, "merge_opaque", "Collapse Hidden Samples");
    Tooltip(f, "Removes samples that are completely hidden behind opaque surfaces, "
            "reducing unnecessary deep data.");

    Float_knob(f, &_rdp_epsilon, "rdp_epsilon", "RDP epsilon");
    Tooltip(f, "Epsilon value for the Ramer-Douglas-Peucker algorithm. "
               "Higher values merge more samples");
    SetFlags(f, Knob::LOG_SLIDER);

    BeginClosedGroup(f, "Advanced");
    SetFlags(f, Knob::STARTLINE);
    Bool_knob(f, &_volumise, "volumise", "Volumise Samples");
    Tooltip(f, "Gives samples depth by extending their back position, "
            "making them behave like volumetric data rather than flat surfaces.");
    SetFlags(f, Knob::STARTLINE);

    Channel_knob(f, &_diagnostic_channel, 1, "diagnostic_channel");
    Tooltip(f, "Outputs merge information to the selected channel, "
            "showing how many source samples contribute to each result.");
    EndGroup(f);

}

int DeepSampleMerge::knob_changed(Knob* k) {
    knob("rdp_epsilon")->visible(_algorithm_choice == RDP);

    knob("dist_threshold")->visible(_algorithm_choice == SIMPLE);
    knob("alpha_threshold")->visible(_algorithm_choice == SIMPLE);
    knob("merge_opaque")->visible(_algorithm_choice == SIMPLE);
    return 1;
}


void DeepSampleMerge::append(Hash& hash) {
    DeepFilterOp::append(hash);

    hash.append(_algorithm_choice);
    hash.append(_use_distance_scaling);

    hash.append(_distance_threshold);
    hash.append(_alpha_threshold);
    hash.append(_merge_opaque);

    hash.append(_rdp_epsilon);

    hash.append(_volumise);
    hash.append(_diagnostic_channel);
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
    output_channels += _diagnostic_channel;

    DeepInPlaceOutputPlane output_plane(output_channels, box);
    output_plane.reserveSamples(input_plane.getTotalSampleCount());

    for (Box::iterator box_it = box.begin(); box_it != box.end(); ++box_it) {
        DeepPixel input_pixel = input_plane.getPixel(box_it);
        const auto sample_count = static_cast<int>(input_pixel.getSampleCount());

        if (sample_count == 0) {
            output_plane.setSampleCount(box_it, 0);
            continue;
        }

        std::vector<SampleRange> samples_to_merge;
        samples_to_merge.reserve(sample_count);
        switch (_algorithm_choice) {
            case SIMPLE:
                should_merge_distance_alpha(samples_to_merge, input_pixel);
                break;
            case RDP:
                should_merge_RDP(samples_to_merge, input_pixel);
                break;
            default: ;
        }

        output_plane.setSampleCount(box_it, samples_to_merge.size());
        DeepOutputPixel output_pixel = output_plane.getPixel(box_it);

        size_t out_sample_index = 0;
        for (const auto& sample_pair : samples_to_merge) {
            assert(sample_pair.last < sample_count);
            merge_sample_range(input_pixel, output_pixel, out_sample_index, sample_pair);
            ++out_sample_index;
        }
    }

    output_plane.reviseSamples();
    out = static_cast<DeepOutputPlane>(output_plane);

    return true;
}

const Op::Description DeepSampleMerge::description(CLASS, build);

static float point_line_distance(const DeepPlotSample& point, const DeepPlotSample& a, const DeepPlotSample& b) {
    float v_x = b.x - a.x;
    float v_y = b.y - a.y;

    if (v_x == 0.0f && v_y == 0.0f) {
        float d_x = point.x - a.x;
        float d_y = point.y - a.y;
        return d_x * d_x + d_y * d_y;
    }

    const float inv_length = 1.0f / (v_x * v_x + v_y * v_y);
    float t = ((point.x - a.x)* v_x +
               (point.y - a.y)* v_y) *
               inv_length;
    t = std::max(0.0f, std::min(1.0f, t));

    float proj_x = a.x + t * v_x;
    float proj_y = a.y + t * v_y;

    float d_x = point.x - proj_x;
    float d_y = point.y - proj_y;

    return d_x * d_x + d_y * d_y;
}

static void ramer_douglas_peucker(const std::vector<DeepPlotSample>& deep_samples, int start, int end, float epsilon, std::vector<uint8_t>& keep) {
    if (end <= start + 1)
        return;

    float max_distance = 0.0f;
    int index = -1;

    const DeepPlotSample& start_sample = deep_samples[start];
    const DeepPlotSample& end_sample = deep_samples[end];

    for (int i = start + 1; i < end; ++i) {
        float distance = point_line_distance(deep_samples[i], start_sample, end_sample);
        if (distance > max_distance) {
            max_distance = sqrt(distance);
            index = i;
        }
    }

    if (max_distance > epsilon) {
        keep[index] = 1;
        ramer_douglas_peucker(deep_samples, start, index, epsilon, keep);
        ramer_douglas_peucker(deep_samples, index, end, epsilon, keep);
    }
}