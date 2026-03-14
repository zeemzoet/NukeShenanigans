//
// Created by Hannes Sap on 08/03/2026.
//
#include "DDImage/Row.h"
#include "DDImage/Knobs.h"
#include "DDImage/Iop.h"
#include "DDImage/DeepOp.h"
#include "DDImage/CameraOp.h"

#include <cmath>
#include <valarray>

const char* CLASS = "DeepCount";
const char* HELP = "Visualises the amount of Deep samples in an image";

using namespace DD::Image;

template <class Type, class Typeb>
inline Type inverse_lerp(Type minimum, Type maximum, Typeb value)
{
    if (std::abs(maximum-minimum) < 0.01f) {
        return static_cast<Type>(0);
    }

    Type result = (value - minimum) / (maximum - minimum);

    return clamp(result, static_cast<Type>(0), static_cast<Type>(1));
}

class DeepCount : public DD::Image::Iop {
private:
    double top_limit;

    DeepOp* my_input() const {
        return dynamic_cast<DeepOp*>(Op::input0());
    }

public:
    explicit DeepCount(Node* node) : Iop(node), top_limit(10.0f) {}

    [[nodiscard]] const char* Class() const override { return CLASS; }
    [[nodiscard]] const char* node_help() const override { return HELP; }
    [[nodiscard]] const char* node_shape() const override { return DeepOp::DeepNodeShape(); }

    [[nodiscard]] Op* default_input(int) const override { return nullptr; }
    [[nodiscard]] int minimum_inputs() const override { return 1; }
    [[nodiscard]] int maximum_inputs() const override { return 1; }
    bool test_input(int idx, Op*op) const override
    {
        return dynamic_cast<DeepOp*>(op);
    }

    void knobs(Knob_Callback) override;

    void _validate(bool) override;
    void _request(int, int, int, int, ChannelMask, int) override;
    void engine(int, int, int, ChannelMask, Row&) override;

    static const Description description;
};

void DeepCount::knobs(Knob_Callback f) {
    Double_knob(f, &top_limit, IRange(0.0, 50.0), "top_limit", "Top Limit");
}

void DeepCount::_validate(bool for_real) {
    if (my_input()) {
        my_input()->validate(true);
        DeepInfo deep_info = my_input()->deepInfo();

        info_.setFormats(deep_info.formats());
        info_.set(deep_info.box());
        info_.channels(deep_info.channels());
        info_.first_frame(deep_info.first_frame());
        info_.last_frame(deep_info.last_frame());

        set_out_channels(Mask_RGBA);

        my_input()->op()->cached(cached());
    }
    else {
        info_.set(Box());
        info_.channels((Mask_None));
    }
}

void DeepCount::_request(int x, int y, int r, int t, ChannelMask channels, int count) {
    ChannelSet required_channels = channels;
    required_channels += Mask_Deep;

    my_input()->deepRequest(Box(x, y, r, t), required_channels, count);
}

void DeepCount::engine(int y, int x, int r, ChannelMask channels, Row& row) {
    DeepOp* deep_input = my_input();

    if (!deep_input) {
        foreach(z, channels) {
            row.erase(z);
        }
        return;
    }

    DeepPlane deep_row;
    ChannelSet required_channels = channels;
    required_channels += Mask_Deep;

    if (!deep_input->deepEngine(y, x, r, required_channels, deep_row)) {
        Iop::abort();
        foreach(z, channels) {
            row.erase(z);
        }
        return;
    }

    ChannelSet done;
    foreach(z, channels) {
        // We do red, green and blue all ot once, but we're
        // still in a for loop for each channel
        // so we track if we've done rgb already and skip the loop if we did.
        if (done & z)
            continue;

        // We check all of them because there is no guarantee
        // Chan_Red is the first on in the foreach loop
        if (z & Mask_RGBA ) done+= Mask_RGBA;

        const float kSampleScale               = 1.0f / top_limit;
        static constexpr float kWhiteMax       = 0.99f;
        static constexpr float kRedMin         = 0.75f;
        static constexpr float kRedMax         = 1.0f;
        static constexpr float kGreenMax       = 0.75f;
        static constexpr float kBlueMax        = 0.75f;


        float* out_r = row.writable(Chan_Red);
        float* out_g = row.writable(Chan_Green);
        float* out_b = row.writable(Chan_Blue);
        float* out_a = row.writable(Chan_Alpha);


        for (int px = x; px < r; ++px, ++out_r, ++out_g, ++out_b, ++out_a) {
            const float sample_count = static_cast<float>(deep_row.getPixel(y, px).getSampleCount());

            *out_a = static_cast<float>(sample_count);

            if (sample_count == 0) {
                *out_r = *out_g = *out_b= 0.0f;
            } else if (const float t = clamp(sample_count * kSampleScale, 0.0f, 1.0f); t >= kWhiteMax) {
                *out_r = *out_g = *out_b = 1.0f;
            } else {
                *out_r = inverse_lerp(kRedMin, kRedMax, t);
                *out_g = inverse_lerp(0.0f, kGreenMax, t) * (1.0f - inverse_lerp(kRedMin, kRedMax, t));
                *out_b = 1.0f - inverse_lerp(0.0f, kBlueMax, t);
            }
        }
    }
}

static Op* build(Node* node) { return new DeepCount(node); }
const Op::Description DeepCount::description(CLASS, "Image/DeepCount", build);