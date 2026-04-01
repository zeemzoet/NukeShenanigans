//
// Created by Hannes Sap on 05/03/2026.
//

#include "DDImage/Iop.h"
#include "DDImage/PlanarIop.h"
#include "DDImage/Thread.h"
#include "DDImage/ParallelFor.h"

#include "DDImage/DeepOp.h"
#include "DDImage/Row.h"
#include <iostream>

const char* CLASS = "DeepHistogram";
const char* HELP = "Shows a histogram of the deep samples in an image";

using namespace DD::Image;

class DeepHistogram : public PlanarIop {
private:
    Box _deep_bbox = Box();

public:
    explicit DeepHistogram(Node* node) : PlanarIop(node) {}

    /* Input methods */
    [[nodiscard]] Op* default_input(int) const override;
    [[nodiscard]] int minimum_inputs() const override;
    [[nodiscard]] int maximum_inputs() const override;
    bool test_input(int, Op*) const override;

    /* Helper methods */
    [[nodiscard]] DeepOp* input_0() const;

    /* Worker methods */
    void _validate(bool) override;
    void getRequests(const Box&, const ChannelSet&, int count, RequestOutput&) const override;
    void renderStripe(ImagePlane&) override;

    /* Registering methods */
    static const Iop::Description node_description;
    [[nodiscard]] const char* Class() const override { return node_description.name; }
    [[nodiscard]] const char* node_help() const override { return HELP; }
};

Op* DeepHistogram::default_input(int) const { return nullptr; }
int DeepHistogram::minimum_inputs() const { return 1; }
int DeepHistogram::maximum_inputs() const { return 1; }
bool DeepHistogram::test_input(int index, Op* op) const {
    return dynamic_cast<DeepOp*>(op);
}

DeepOp* DeepHistogram::input_0() const { return dynamic_cast<DeepOp*>(Op::input(0)); }

void DeepHistogram::_validate(bool for_real) {
    if (input_0()) {
        input_0()->validate(true);
        DeepInfo deep_info = input_0()->deepInfo();

        //Convert a deep_info to the regular 2D info_
        Box image_box = Box(
            deep_info.formats().format()->x(),
            deep_info.formats().format()->y(),
            deep_info.formats().format()->r(),
            deep_info.formats().format()->t()
        );
        _deep_bbox = deep_info.box();
        info_.set(image_box);
        info_.setFormats(deep_info.formats());
        info_.channels(deep_info.channels());
        info_.first_frame(deep_info.firstFrame());
        info_.last_frame(deep_info.lastFrame());
        info_.black_outside(true);

        set_out_channels(Mask_RGBA);
        input_0()->op()->cached(cached());
    }
    else {
        info_.set(Box());
        info_.channels(Mask_None);
    }
}

void DeepHistogram::getRequests(const Box &box, const ChannelSet &channels, int count, RequestOutput &reqData) const {
    ChannelSet new_channels = channels;
    new_channels += Mask_DeepBack;
    new_channels += Mask_DeepFront;

    input_0()->deepRequest(_deep_bbox, new_channels, count);
}


void DeepHistogram::renderStripe(ImagePlane& image_plane) {
    image_plane.makeWritable();
    const Box box = image_plane.bounds();
    const ChannelSet channels =  image_plane.channels();

    for (Box::iterator it = box.begin(); it != box.end(); ++it) {
        foreach(z, channels) {
            image_plane.writableAt(it.x, it.y, image_plane.chanNo(z)) = 0;
        }
    }

    DeepPlane deep_row;
    ChannelSet extra_channels = channels;
    extra_channels += Mask_DeepFront;
    extra_channels += Mask_DeepBack;

    if (!input_0()->deepEngine(_deep_bbox, extra_channels, deep_row)) {
        PlanarIop::abort();
        foreach(z, channels) {
            image_plane.clear();
        }
        return;
    }

    std::cout << deep_row.getTotalSampleCount() << std::endl;

    float max {0.0f};
    for (Box::iterator it = box.begin(); it != box.end(); ++it) {
        if (!_deep_bbox.contains(Box(it.x, it.y, it.x+1, it.y+1))) continue;
        DeepPixel deep_pixel = deep_row.getPixel(it.y, it.x);
        for (int s=0; s<deep_pixel.getSampleCount(); s++) {
            float sample = deep_pixel.getUnorderedSample(s, Chan_DeepFront);
            if (sample > max) {max = sample;}
        }
    }
    // 1068, 1673
    for (Box::iterator it = box.begin(); it != box.end(); ++it) {
        if (!_deep_bbox.contains(Box(it.x, it.y, it.x+1, it.y+1))) continue;
        DeepPixel deep_pixel = deep_row.getPixel(it.y, it.x);
        const int sample_count = deep_pixel.getSampleCount();
        if (sample_count == 0) continue;

        for (int s=0; s<sample_count; s++) {
            float front_sample = deep_pixel.getUnorderedSample(s, Chan_DeepFront)/max*box.t();
            float back_sample = deep_pixel.getUnorderedSample(s, Chan_DeepBack)/max*box.t();
            foreach(z, channels){
                float existing_pixel = image_plane.at(it.x, it.y, z);
                float deep_color = deep_pixel.getUnorderedSample(s, z);
                for (int _y=front_sample; _y<=back_sample; _y++) {
                    if (_y >= box.y() && _y < box.t()) {
                        float step_size = (back_sample==front_sample) ? 1.0f : 1.0f/abs(back_sample - front_sample);
                        image_plane.writableAt(it.x, _y, image_plane.chanNo(z)) = (deep_color*step_size);
                    }
                }
            }
        }
    }
}

static Iop* build(Node* node) { return new DeepHistogram(node); }
const Iop::Description DeepHistogram::node_description(CLASS, build);
