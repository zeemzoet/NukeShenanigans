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

    int knobs_amount_;

    int channels_amount_;
    std::vector<ChannelSet> dynamic_channels_ = {Mask_None, Mask_None, Mask_None, Mask_None};
    std::vector<std::string> channel_names_ = {"none", "none", "none", "none"};
    void resize_channel_arrays() {
        dynamic_channels_.resize(get_amount(), Mask_None);
        channel_names_.resize(get_amount(), "none");
    }

    static void add_dynamic_channelknobs(void*, Knob_Callback);

public:
    explicit RemoveUnlimited(Node* node)
        : NoIop(node)
        , operation_(1)
        , knobs_amount_(0)
        , channels_amount_(2)
    {}

    void _validate(bool) override;
    void knobs(Knob_Callback) override;
    int knob_changed(Knob *) override;
    std::vector<ChannelSet>& get_dynamic_channels() {return dynamic_channels_;}

    [[nodiscard]] int get_amount() const {
        //Because KNOB_CHANGED_ALWAYS set, can't use channels_amount_ directly.
        //Unless the knob does not exist yet of course.
        if (const Knob* amount = knob("channels_amount"))
            return static_cast<int>(amount->get_value());
        return channels_amount_;
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

    Int_knob(f, &channels_amount_, "channels_amount", "Amount of Channels:");
    SetFlags(f, Knob::KNOB_CHANGED_ALWAYS);

    if (!f.makeKnobs())
        add_dynamic_channelknobs(this->firstOp(), f);
    else
        knobs_amount_ = add_knobs(add_dynamic_channelknobs, this->firstOp(), f);
}

int RemoveUnlimited::knob_changed(Knob *k) {
    if (k->is("channels_amount")) {
        knobs_amount_ = replace_knobs(
            knob("channels_amount"),
            knobs_amount_,
            add_dynamic_channelknobs,
            this->firstOp()
        );
        return 1;
    }
    return 1;
}

void RemoveUnlimited::add_dynamic_channelknobs(void* p, Knob_Callback f) {
    if (auto* node_op = static_cast<RemoveUnlimited*>(p)) {
        node_op->resize_channel_arrays();
        for (size_t index = 0; index < node_op->get_dynamic_channels().size(); ++index) {
            ChannelSet& channels = node_op->get_dynamic_channels()[index];
            std::string knob_name = "channels" + std::to_string(index + 1);

            // Store channels as strings
            if (!f.makeKnobs()) {
                if (const Knob* existing_knob = node_op->knob(knob_name.c_str())) {
                    std::stringstream ss;
                    existing_knob->to_script(ss, nullptr, false);
                    node_op->channel_names_[index] = ss.str();
                }
            }

            // Create new knobs
            channels = f.makeKnobs() ? Mask_None : channels;  // Ensure channels is Mask_None when we'll use from_script
            Knob* new_knob = Input_ChannelMask_knob(f, &channels, 0, knob_name.c_str());

            // Restore channel from the stored strings
            // Thank you, Bart Ashworth!
            if (new_knob && f.makeKnobs()) {
                new_knob->from_script(node_op->channel_names_[index].c_str());
            }
        }
    }
}