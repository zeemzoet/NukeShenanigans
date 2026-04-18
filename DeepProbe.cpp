//
// Created by Hannes Sap on 03/04/2026.
//
#include <DDImage/DeepOp.h>
#include <sys/stat.h>

#include "DDImage/Knobs.h"
#include "DDImage/DeepPixelOp.h"
#include "DDImage/Pixel.h"
#include "ProbeWidget.moc.h"

using namespace  DD::Image;

static const char* CLASS = "DeepProbe";
static const char* HELP = "Visualises a deep pixel at a given point";

class DeepProbe : public DeepPixelOp {
    float coordinate_[2] = {0.0f, 0.0f};
    DummyDeepPixel deep_pixel_ {};

    void sample_deep_pixel();

public:
    explicit DeepProbe(Node* node) : DeepPixelOp(node) {}

    [[nodiscard]] const char* Class() const override { return CLASS; }
    [[nodiscard]] const char* node_help() const override { return HELP; }
    static const Description description;

    void _validate(bool) override;
    void append(Hash &) override;
    void processSample(int, int, const DeepPixel&, size_t, const ChannelSet&, DeepOutPixel&) const override;

    void knobs(Knob_Callback) override;
    int knob_changed(Knob*) override;
};

void DeepProbe::_validate(bool for_real) {
    DeepPixelOp::_validate(for_real);
    input0()->op()->cached(cached());
    if (for_real)
        sample_deep_pixel();
}

void DeepProbe::append(Hash& hash) {
    hash.append(coordinate_[0]);
    hash.append(coordinate_[1]);
}

void DeepProbe::processSample(int y,
                                int x,
                                const DeepPixel &input_pixel,
                                size_t sampleNo,
                                const ChannelSet &channels,
                                DeepOutPixel& output_pixel) const {
    Pixel out_pixel(channels);
    foreach(z, channels) {
        out_pixel[z] = input_pixel.getUnorderedSample(sampleNo, z);
        output_pixel.push_back(out_pixel[z]);
    }

}

void DeepProbe::knobs(Knob_Callback f) {
    XY_knob(f, coordinate_, "Sample");
    CustomKnob1(ProbeKnob, f, &deep_pixel_, "WidgetKnob");
}

int DeepProbe::knob_changed(Knob* knob) {
    if (knob->is("Sample") || knob == &Knob::showPanel) {
        sample_deep_pixel();
        return 1;
    }
    return 0;
}

void DeepProbe::sample_deep_pixel() {
    const int x = static_cast<int>(coordinate_[0]);
    const int y = static_cast<int>(coordinate_[1]);

    auto* deep_input = dynamic_cast<DeepOp*>(this->input(0));
    if (!deep_input)
        return;

    if (DeepPlane tmp_deep_plane; deep_input->deepEngine(y, x, x+1, DummyDeepPixel::requested_channels, tmp_deep_plane)) {
        deep_pixel_ = DummyDeepPixel(tmp_deep_plane.getPixel(y, x));
        knob("WidgetKnob")->changed();
    }
}

static Op* build(Node* node) { return new DeepProbe(node); }
const Op::Description DeepProbe::description("DeepProbe", build);