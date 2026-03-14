//
// Created by Hannes Sap on 14/03/2026.
//

#include <DDImage/CameraOp.h>
#include <DDImage/Knob.h>

const char* CLASS = "DummyCamera";
const char* HELP = "A cpp version of the DummyCam gizmp";

using namespace DD::Image;

class DummyCamera : public CameraOp {

public:
    explicit DummyCamera(Node* node) : CameraOp(node) {
    }

    //[[nodiscard]] const char* node_shape() const override {return "r";}
    [[nodiscard]] int minimum_inputs() const override { return 1; }
    [[nodiscard]] int maximum_inputs() const override { return 1; }
    [[nodiscard]] Op* default_input(int) const override { return default_camera(); }
    bool test_input(int, Op* op) const override {return op->cameraOp(); }
    const char* input_label(int, char *) const override {return "camera";}

    void _validate(bool) override;
    void knobs(Knob_Callback) override;
    int knob_changed(Knob *) override;

    static Op* Build(Node* node) { return new DummyCamera(node); }
    static const Op::Description description;
};

void DummyCamera::_validate(bool for_real) {
    if (parentInputOp()) {
        parentInputOp()->_validate(for_real);
    }

    if (const auto* camera_input = Op::input0()->cameraOp()) {
        setProjectionFunc(camera_input->projectionFunc());
        setFocalLength(camera_input->focalLength());
        setHorizontalAperture(camera_input->horizontalAperture());
        setVerticalAperture(camera_input->verticalAperture());
        setNearPlaneDistance(camera_input->nearPlaneDistance());
        setFarPlaneDistance(camera_input->farPlaneDistance());
    }
    else {
        setProjectionFunc(default_camera()->projectionFunc());
    }

    CameraOp::_validate(for_real);
}

void DummyCamera::knobs(Knob_Callback callback) {
    // TODO: add me custom knobs!
    //CameraOp::addProjectionKnobs(callback, false);
    CameraOp::addLensKnobs(callback);
}

int DummyCamera::knob_changed(Knob* k) {
    // TODO: implement custom knob changed
    if (const auto* camera_input = Op::input0()->cameraOp()) {
        return 1;
    }
    return 0;
}


const Op::Description DummyCamera::description(CLASS, DummyCamera::Build);