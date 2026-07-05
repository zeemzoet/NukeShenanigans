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
    float x, y;
    DeepPlotSample(const float x, const float y) : x(x), y(y) {}
};

static void ramer_douglas_peucker(const std::vector<DeepPlotSample>&, int, int, float, std::vector<char>&);

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

    float _rdp_epsilon {0.01f};

    bool _volumise { true };
    bool _merge_opaque { true };
    Channel _diagnostic_channel;

    void merge_sample_pairs(const DeepPixelBuffer&, size_t, const ChannelMap&, const SampleRange&) const;
    void should_merge_distance_alpha(std::vector<SampleRange>&, const DeepPixelBuffer&, int) const;
    void should_merge_RDP(std::vector<SampleRange>&, const DeepPixelBuffer&, int) const;

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

void DeepSampleMerge::merge_sample_pairs(const DeepPixelBuffer &buffer, const size_t index, const ChannelMap &channels,
                                    const SampleRange& sample_pair) const {
    assert(sample_pair.first <= sample_pair.last);

    ChannelSet other_channels(channels);
    other_channels -= Chan_Alpha;
    other_channels -= Chan_DeepFront;
    other_channels -= Chan_DeepBack;
    other_channels -= _diagnostic_channel;

    const size_t out_index = index * buffer.output_channel_size;
    float total_transparency = 1.0f;  //transparency = (1.0 - alpha)
    for(size_t s = sample_pair.first; s <= sample_pair.last; ++s) {
        const float transparency = 1.0f - buffer.input_data[Chan_Alpha][s * buffer.input_channel_size];
        foreach(z, other_channels) {
            const float* src = buffer.input_data[z];
            float*       dst = buffer.output_data[z];
            dst[out_index] += src[s * buffer.input_channel_size] * total_transparency;
        }
        total_transparency *= transparency;
    }
    buffer.output_data[Chan_Alpha][out_index] = 1.0f - total_transparency;
    if (_diagnostic_channel != Chan_Black)
        buffer.output_data[_diagnostic_channel][out_index] = static_cast<float>(sample_pair.last-sample_pair.first);

    buffer.output_data[Chan_DeepFront][out_index] = buffer.input_data[Chan_DeepFront][sample_pair.last * buffer.input_channel_size];
    buffer.output_data[Chan_DeepBack][out_index] = _volumise
                                                       ? buffer.input_data[Chan_DeepBack][sample_pair.first * buffer.input_channel_size]
                                                       : buffer.input_data[Chan_DeepFront][sample_pair.last * buffer.input_channel_size];
}

void DeepSampleMerge::should_merge_distance_alpha(std::vector<SampleRange>& samples_to_merge, const DeepPixelBuffer& buffer, const int sample_count) const {
    auto should_merge = [&](size_t lower_index, size_t upper_index) {
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
    };

    const float* alpha_buffer = buffer.input_data[Chan_Alpha];
    float accumulated_transparency = 1.0f;
    for (int sample = sample_count - 1; sample >= 0; --sample) {
        const float alpha = alpha_buffer[sample * buffer.input_channel_size];
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

void DeepSampleMerge::should_merge_RDP(std::vector<SampleRange>& samples_to_merge, const DeepPixelBuffer& buffer, int sample_count) const {
    // First we make a graph, plotting the deep samples, remembering which
    // original sample each plotted point came from.
    std::vector<DeepPlotSample> deep_plotted;
    std::vector<uint16_t> plotted_sample_index;
    deep_plotted.reserve(sample_count * 2);
    plotted_sample_index.reserve(sample_count * 2);

    float total_transparency = 1.0f;
    float total_avg_depth = 0.0f;
    // for (int sample = sample_count - 1; sample >= 0; --sample)
    for (int sample = 0; sample < sample_count; ++sample) {
        const size_t sample_index = sample * buffer.input_channel_size;

        float front = buffer.input_data[Chan_DeepFront][sample_index];
        float back  = buffer.input_data[Chan_DeepBack][sample_index];
        const float alpha = buffer.input_data[Chan_Alpha][sample_index];

        total_avg_depth += (front + back) / 2;
        front = _use_distance_scaling ? front / total_avg_depth : front;
        back  = _use_distance_scaling ? back / total_avg_depth : back;

        deep_plotted.emplace_back(front, 1.0f - total_transparency);
        plotted_sample_index.push_back(sample);

        if (front != back) {
            deep_plotted.emplace_back(back, 1.0f - total_transparency);
            plotted_sample_index.push_back(sample);
        }

        total_transparency *= (1.0f - alpha);
    }

    const int deep_plotted_size = static_cast<int>(deep_plotted.size());
    samples_to_merge.reserve(deep_plotted_size);

    if (deep_plotted_size < 3) {
        for (int i = 0; i < deep_plotted_size; ++i)
            samples_to_merge.emplace_back(plotted_sample_index[i], plotted_sample_index[i]);
        return;
    }

    std::vector<char> keep(deep_plotted_size, 0);
    keep[0] = 1;
    keep[deep_plotted_size - 1] = 1;

    ramer_douglas_peucker(deep_plotted, 0, deep_plotted_size - 1, _rdp_epsilon, keep);

    for (size_t i = 0; i < deep_plotted_size; ++i) {
        const uint16_t orig_sample = plotted_sample_index[i];
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

    Bool_knob(f, &_use_distance_scaling, "use_distance_scaling", "Scale Merge Distance by Depth");
    Tooltip(f, "Increases the merge distance for samples further away, "
            "allowing more aggressive merging at greater depths.");

    const auto dist_knob = Float_knob(f, &_distance_threshold, "dist_threshold", "Sample Merge Distance");
    dist_knob->visible(_algorithm_choice == SIMPLE);
    Tooltip(f, "Maximum distance between deep samples before they are merged together.");
    SetFlags(f, Knob::LOG_SLIDER);
    const auto alpha_knob = Float_knob(f, &_alpha_threshold, "alpha_threshold", "Alpha Merge Limit");
    alpha_knob->visible(_algorithm_choice == SIMPLE);
    Tooltip(f, "Samples with alpha values above this threshold stop further merging, "
            "helping preserve sharp opacity transitions.");
    const auto opaque_knob = Bool_knob(f, &_merge_opaque, "merge_opaque", "Collapse Hidden Samples");
    opaque_knob->visible(_algorithm_choice == SIMPLE);
    Tooltip(f, "Removes samples that are completely hidden behind opaque surfaces, "
            "reducing unnecessary deep data.");

    const auto rdp_knob = Float_knob(f, &_rdp_epsilon, "rdp_epsilon", "RDP epsilon");
    rdp_knob->visible(_algorithm_choice == RDP);
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
    if (k->is("method")) {
        const bool method_value = static_cast<bool>(k->get_value());
        knob("rdp_epsilon")->visible(method_value == RDP);

        knob("dist_threshold")->visible(method_value == SIMPLE);
        knob("alpha_threshold")->visible(method_value == SIMPLE);
        knob("merge_opaque")->visible(method_value == SIMPLE);
        return 1;
    }
    return 0;
}


void DeepSampleMerge::append(Hash& hash) {
    hash.append(_use_distance_scaling);
    hash.append(_distance_threshold);
    hash.append(_alpha_threshold);
    hash.append(_volumise);
    hash.append(_merge_opaque);
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

    DeepPixelBuffer buffer;
    buffer.input_channel_size = input_plane.channels().size();
    buffer.output_channel_size = output_plane.channels().size();

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

        if (!buffer.input_data[Chan_Alpha]) continue;

        std::vector<SampleRange> samples_to_merge;
        samples_to_merge.reserve(sample_count);
        switch (_algorithm_choice) {
            case SIMPLE:
                should_merge_distance_alpha(samples_to_merge, buffer, sample_count);
                break;
            case RDP:
                should_merge_RDP(samples_to_merge, buffer, sample_count);
                break;
            default: ;
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
        for (const auto& sample_pair : samples_to_merge) {
            assert(sample_pair.last < sample_count);
            merge_sample_pairs(buffer, out_sample_index, output_channel_map, sample_pair);
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

    if (v_x == 0.0f && v_y == 0.0f)
        return std::hypot(point.x - a.x, point.y - a.y);

    float t = ((point.x - a.x)* v_x + (point.y - a.y)* v_y) / (v_x * v_x + v_y * v_y);
    t = std::max(0.0f, std::min(1.0f, t));

    float proj_x = a.x + t * v_x;
    float proj_y = a.y + t * v_y;

    float d_x = point.x - proj_x;
    float d_y = point.y - proj_y;

    return std::sqrt(d_x * d_x + d_y * d_y);
}

static void ramer_douglas_peucker(const std::vector<DeepPlotSample>& deep_samples, int start, int end, float epsilon, std::vector<char>& keep) {
    if (end <= start + 1)
        return;

    float max_distance = 0.0f;
    int index = -1;

    const DeepPlotSample& start_sample = deep_samples[start];
    const DeepPlotSample& end_sample = deep_samples[end];

    for (int i = start + 1; i < end; ++i) {
        float distance = point_line_distance(deep_samples[i], start_sample, end_sample);
        if (distance > max_distance) {
            max_distance = distance;
            index = i;
        }
    }

    if (max_distance > epsilon) {
        keep[index] = 1;
        ramer_douglas_peucker(deep_samples, start, index, epsilon, keep);
        ramer_douglas_peucker(deep_samples, index, end, epsilon, keep);
    }
}