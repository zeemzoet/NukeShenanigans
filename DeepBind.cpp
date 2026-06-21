//
// Created by Hannes Sap on 16/06/2026.
//
#include <complex>
#include <DDImage/DeepFilterOp.h>
#include <DDImage/Iop.h>
#include <DDImage/Knobs.h>
#include <DDImage/Row.h>
#include <chrono>

#define TIMINGS false

using namespace DD::Image;

static const char* CLASS = "DeepBind";
static const char* HELP = "Bind 2D and Deep data together";

class DeepBind : public DeepFilterOp {

    bool _remap_alpha;

    static ChannelSet get_deep_channels() {
        ChannelSet deep_channels(Mask_Deep);
        deep_channels += Mask_Alpha;
        return deep_channels;
    }
    [[nodiscard]] ChannelSet get_colour_channels(ChannelSet channels) const {
        if (!_remap_alpha)
            channels -= Mask_Alpha;
        channels -= Mask_Deep;
        return channels;
    }

    [[nodiscard]] Iop* image_input() const { return dynamic_cast<Iop*>(input(0)); }
    [[nodiscard]] DeepOp* deep_input() const { return dynamic_cast<DeepOp*>(input(1)); }

public:
    explicit DeepBind(Node* node) : DeepFilterOp(node), _remap_alpha(true) {}

    void _validate(bool) override;
    void getDeepRequests(Box, const ChannelSet&, int, std::vector<RequestData>&) override;
    bool doDeepEngine(Box, const ChannelSet&, DeepOutputPlane&) override;

    void knobs(Knob_Callback) override;

    [[nodiscard]] int minimum_inputs() const override { return 2; }
    [[nodiscard]] int maximum_inputs() const override{ return 2; }
    const char* input_label(int, char*) const override;
    bool test_input(int, Op*) const override;

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

void DeepBind::knobs(Knob_Callback f) {
    Bool_knob(f, &_remap_alpha, "use_input_alpha", "Use Input Alpha");
}

void DeepBind::_validate(bool for_real) {
    Box total_box = Box();
    ChannelSet total_channels(Mask_None);
    FormatPair total_formats = FormatPair();
    if (const auto image = image_input()) {
        image->validate(for_real);
        const auto& info = image->info();

        total_box.merge(info.box());
        total_channels += info.channels();
        total_formats = info.formats();
    }
    if (const auto deep = deep_input()) {
        deep->validate(for_real);
        const auto& deepinfo = deep->deepInfo();

        total_box.merge(deepinfo.box());
        total_channels += deepinfo.channels();
        total_formats = deepinfo.formats();
    }
    _deepInfo = DeepInfo(total_formats, total_box, total_channels);
}

void DeepBind::getDeepRequests(Box box, const ChannelSet& channels, int count, std::vector<RequestData>& requests) {
    if (const auto image = image_input()) {
        image->request(box, get_colour_channels(channels), count);
    }
    if (const auto deep = deep_input()) {
        deep->deepRequest(box, get_deep_channels(), count);
    }
}


bool DeepBind::doDeepEngine(Box box, const ChannelSet& channels, DeepOutputPlane& out) {
    if (!deep_input())
        return false;

    DeepPlane input_plane;
    ChannelSet needed_channels = ChannelSet();
    needed_channels += Mask_Alpha;
    needed_channels += Mask_Deep;

    if (!deep_input()->deepEngine(box, needed_channels, input_plane))
        return false;

    DeepInPlaceOutputPlane output_plane(channels, box);

    // precompute chanNo lookups once — outside all loops
    const size_t input_channel_size = input_plane.channels().size();
    const size_t output_channel_size = channels.size();

    // per-channel index table, built once
    // indexed by channel enum, just like the reference code
    const int last_channel_index = channels.last() + 1;

    // --- pointer table: input_data[z] points to sample 0 of channel z ---
    // one pointer per channel enum slot, stack allocated
    //std::vector<const float*> input_data(last_channel_index);
    //std::vector<float*> output_data(last_channel_index);
    const float* input_data[Chan_Last] = {nullptr};
    float* output_data[Chan_Last] = {nullptr};
    std::vector<float> alpha_samples;
    alpha_samples.reserve(100);  //hoping for the best and not too many samples

    // iterate over y
    for (int y = box.y(); y < box.t(); ++y) {

        Row input_row(box.x(), box.r());
        image_input()->get(y, box.x(), box.r(), get_colour_channels(channels), input_row);

#if TIMINGS
        auto t0 = std::chrono::high_resolution_clock::now();
#endif

        // iterate over x
        for (int x = box.x(); x < box.r(); ++x) {

            DeepPixel input_pixel = input_plane.getPixel(y, x);
            const size_t sample_count = input_pixel.getSampleCount();

            output_plane.setSampleCount(y, x, sample_count);
            DeepOutputPixel output_pixel = output_plane.getPixel(y,x);

            const float* in_array  = input_pixel.data();
            float*       out_array = output_pixel.writable();

            foreach(z, channels) {
                input_data[z]  = in_array  + input_plane.channels().chanNo(z);   // sample s is input_data[z][s * channel_size]
                output_data[z] = out_array + output_plane.channels().chanNo(z);
            }

            if (sample_count == 0)
                continue;

            if (alpha_samples.size() < sample_count)
                alpha_samples.resize(sample_count);

            const float* a = input_data[Chan_Alpha];
            float total_transparency = 1.0f;
            for (size_t s = 0; s < sample_count; ++s) {
                float alpha = a[s * input_channel_size];
                total_transparency *= (1.0f - alpha);
                alpha_samples[s] = alpha;
            }

            const float image_alpha = _remap_alpha ? input_row[Chan_Alpha][x] : 1.0f;
            const float alpha_scale = image_alpha / (1.0f - total_transparency);
            if (_remap_alpha) {
                float input_transparency = 1.0f;
                float output_transparency = 1.0f;
                for (size_t s = 0; s < sample_count; ++s) {
                    input_transparency *= 1.0f - alpha_samples[s];

                    float target_transparency = 1.0f - (1.0f - input_transparency) * alpha_scale;
                    if (output_transparency > 1e-6f)
                        alpha_samples[s] = 1.0f - target_transparency / output_transparency;
                    else
                        alpha_samples[s] = 1.0f;
                    output_transparency = target_transparency;
                }
            }

            foreach(z, ChannelSet(Mask_Deep)) {
                const float* src = input_data[z];
                float*       dst = output_data[z];
                for (size_t s = 0; s < sample_count; ++s)
                    dst[s * output_channel_size] = src[s * input_channel_size];
            }

            for (size_t s = 0; s < sample_count; ++s)
                output_data[Chan_Alpha][s * output_channel_size] = alpha_samples[s];

            foreach(z, get_colour_channels(channels)) {
                const float row_val = input_row[z][x];
                float*      dst     = output_data[z];
                for (size_t s = 0; s < sample_count; ++s) {
                    if (_remap_alpha)
                        dst[s * output_channel_size] = row_val / image_alpha * alpha_samples[s];
                    else
                        dst[s * output_channel_size] = row_val * alpha_samples[s];
                }
            }
        }
#if TIMINGS
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        printf("%f\n", ms);
#endif
    }

    out = static_cast<DeepOutputPlane>(output_plane);

    return true;
}