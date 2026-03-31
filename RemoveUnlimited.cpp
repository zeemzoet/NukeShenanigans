//
// Created by Hannes Sap on 28/03/2026.
//
#include <vector>
#include "DDImage/NoIop.h"
#include "DDImage/Knobs.h"
using namespace DD::Image;

static const char* const CLASS = "RemoveUnlimited";
static const char* const HELP = "Remove unlimited amount of channels";


class RemoveUnlimited : public NoIop {
    int operation_; // 0 = remove, 1 = keep

    ChannelSet channels_ {Mask_None};

    int test_amount {0};

    int knobs_amount {0};
    std::vector<ChannelSet> dynamic_channels_;
    std::vector<std::string> channel_names_;
    void resize_channel_arrays() {
        dynamic_channels_.resize(get_amount(), Mask_None);
        channel_names_.resize(get_amount(), "none");
    }

    static void add_dynamic_channelknobs(void*, Knob_Callback);

public:
    explicit RemoveUnlimited(Node* node) : NoIop(node), operation_(1) {
        //dynamic_channels_ = {Mask_Red, Mask_Green};
    }

    void _validate(bool) override;
    void knobs(Knob_Callback) override;
    int knob_changed(Knob *) override;
    std::vector<ChannelSet>& get_dynamic_channels() {return dynamic_channels_;}

    [[nodiscard]] int get_amount() const {
        return static_cast<int>(knob("Testing")->get_value());
    }

    [[nodiscard]] const char* Class() const override { return CLASS; }
    [[nodiscard]] const char* node_help() const override { return HELP; }
    static Iop::Description description;
};

static Iop* build_node(Node* node) { return new RemoveUnlimited(node); };
Iop::Description RemoveUnlimited::description(CLASS, build_node);

void RemoveUnlimited::_validate(bool for_real) {
    copy_info();
    ChannelSet c;
    c += channels_;
    for (const auto& new_channels : dynamic_channels_) {
        c += new_channels;
    }

    if (operation_ == 1) {  // Keep
        // intersect channels
        info_.channels() &= c;
        set_out_channels(info_.channels());
    }
    else {  // Remove
        info_.turn_off(c);
        set_out_channels(c);
    }
}

static const char* const operation_enums[] = {
    "remove", "keep", nullptr
  };

void RemoveUnlimited::knobs(Knob_Callback f) {
    Enumeration_knob(f, &operation_, operation_enums, "operation");

    Int_knob(f, &test_amount, "Testing");
    SetFlags(f, Knob::KNOB_CHANGED_ALWAYS);

    Input_ChannelMask_knob(f, &channels_, 0, "channels");

    if (!f.makeKnobs()) add_dynamic_channelknobs(this->firstOp(), f);
}

int RemoveUnlimited::knob_changed(Knob *k) {
    if (k->is("Testing")) {
        knobs_amount = replace_knobs(
            knob("channels"),
            knobs_amount,
            add_dynamic_channelknobs,
            this->firstOp()
        );
        return 1;
    }
    return 1;
}

void RemoveUnlimited::add_dynamic_channelknobs(void* p, Knob_Callback f) {
    if (auto* node_op = static_cast<RemoveUnlimited*>(p); node_op->get_amount() > 0) {
        node_op->resize_channel_arrays();
        size_t index {0};
        for ( ChannelSet& c: node_op->get_dynamic_channels() ) {
            std::string knob_name = "channels" + std::to_string(index);

            // Store channel names
            if (!f.makeKnobs()) {
                if (Knob* existing_knob = node_op->knob(knob_name.c_str())) {
                    std::stringstream ss;
                    existing_knob->to_script(ss, nullptr, false);
                    node_op->channel_names_[index] = ss.str();
                }
            }

            // Create new knobs
            Knob* new_knob = Input_ChannelMask_knob(f, &c, 0, knob_name.c_str());

            // Restore channel from the stored strings
            // Thank you Bart Ashworth!
            if (f.makeKnobs() && new_knob) {
                new_knob->from_script(node_op->channel_names_[index].c_str());
            }

            ++index;
        }
    }
}