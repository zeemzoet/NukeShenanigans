//
// Created by Hannes Sap on 16/06/2026.
//
#include <complex>
#include <DDImage/DeepFilterOp.h>
#include <DDImage/Iop.h>
#include <DDImage/Knobs.h>
#include <DDImage/Row.h>
#include <chrono>

using namespace DD::Image;

static const char* CLASS = "DeepBind";
static const char* HELP = "Bind 2D and Deep data together";

class DeepBind : public DeepFilterOp {

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
        ChannelSet colour_channels = channels;
        colour_channels -= Mask_Alpha;
        colour_channels -= Mask_Deep;
        image->request(box, colour_channels, count);
    }
    if (const auto deep = deep_input()) {
        ChannelSet deep_channels(Mask_Deep);
        deep_channels += Mask_Alpha;
        deep->deepRequest(box, deep_channels, count);
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

    ChannelSet colour_channels = channels;
    colour_channels -= Mask_Alpha;
    colour_channels -= Mask_Deep;

    ChannelSet deep_channels(Mask_Deep);
    deep_channels += Mask_Alpha;

    DeepInPlaceOutputPlane output_plane(channels, box);

    // precompute chanNo lookups once — outside all loops
    const size_t input_channel_size = input_plane.channels().size();
    const size_t output_channel_size = channels.size();

    // per-channel index table, built once
    // indexed by channel enum, just like the reference code
    const int last_channel_index = channels.last() + 1;

    // --- pointer table: input_data[z] points to sample 0 of channel z ---
    // one pointer per channel enum slot, stack allocated
    std::vector<const float*> input_data(last_channel_index);
    std::vector<float*> output_data(last_channel_index);
    std::vector<float> alpha;

    // iterate over y
    for (int y = box.y(); y < box.t(); ++y) {

        Row input_row(box.x(), box.r());
        image_input()->get(y, box.x(), box.r(), colour_channels, input_row);

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

            if (alpha.size() < sample_count)
                alpha.resize(sample_count);

            const float* a = input_data[Chan_Alpha];
            for (size_t s = 0; s < sample_count; ++s)
                alpha[s] = a[s * input_channel_size];

            foreach(z, deep_channels) {
                const float* src = input_data[z];
                float*       dst = output_data[z];
                for (size_t s = 0; s < sample_count; ++s)
                    dst[s * output_channel_size] = src[s * input_channel_size];
            }

            foreach(z, colour_channels) {
                const float row_val = input_row[z][x];
                float*      dst     = output_data[z];
                for (size_t s = 0; s < sample_count; ++s)
                    dst[s * output_channel_size] = row_val * alpha[s];
            }
        }
    }

    out = static_cast<DeepOutputPlane>(output_plane);

    return true;
}