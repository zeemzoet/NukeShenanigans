//
// Created by Hannes Sap on 03/04/2026.
//

#ifndef PROBE_WIDGET_H
#define PROBE_WIDGET_H

#include <QObject>
#include <QWidget>
#include <QDoubleSpinBox>
#include <QSlider>
#include "DDImage/Knobs.h"
#include "DDImage/DeepPixel.h"

using namespace DD::Image;

// forward declaration
class ProbeWidget;
class ProbeKnob;

// Callback functor
struct NukeWidgetCallback final : Knob::NotificationCallbackFunctor {
    ProbeWidget* widget_;

    explicit NukeWidgetCallback(ProbeWidget*);
    bool operator==(const Knob::NotificationCallbackFunctor& other) const override;
    int operator()(Knob::NotificationCallbackReason reason) const override;
};

struct NukeWidgetVisibilityCallback final : Knob::VisibilityCallbackFunctor {
    ProbeWidget* widget_;

    explicit NukeWidgetVisibilityCallback(ProbeWidget*);
    bool operator==(const Knob::VisibilityCallbackFunctor& other) const override;
    int operator()() const override;
};

struct DummyDeepPixel {
private:
    float get_unordered_sample(size_t, Channel) const;

public:
    size_t sample_count {0};
    const float* data {nullptr};
    static const ChannelMap requested_channels;

    DummyDeepPixel() = default;
    explicit DummyDeepPixel(const DeepPixel&);

    void draw(QPainter&, int, int, float);
};

// QWidget used in ProbeKnob
class ProbeGraphic : public QWidget {
    Q_OBJECT

    float zoom_;
    DummyDeepPixel* deep_pixel_;

public:
    explicit ProbeGraphic(DummyDeepPixel*, QWidget* parent = nullptr);
    ~ProbeGraphic() override;

    void paintEvent(QPaintEvent*) override;
    void zoom(double zoom);
};

class ProbeWidget : public QWidget {
    Q_OBJECT

    ProbeKnob* nuke_knob_;
    NukeWidgetCallback callback_;
    NukeWidgetVisibilityCallback visibility_callback_;

    QLayout* layout_;
    ProbeGraphic* graph_;
    QDoubleSpinBox* spin_box_;
    QSlider* slider_;

    //signals
    void on_slider_moved(int);
    void on_spin_changed(double);
    void apply_zoom(double) const;

public:
    explicit ProbeWidget(ProbeKnob*, QWidget* parent = nullptr);
    ~ProbeWidget() override;

    void changed() const;
    void update();
    void destroy();
};

// Nuke Knob
class ProbeKnob : public Knob {
    friend class ProbeWidget;

    DummyDeepPixel* deep_pixel_;

public:
    explicit ProbeKnob(DD::Image::Knob_Closure*, DummyDeepPixel* , const char*);

    const char* Class() const override { return "ProbeKnob"; }
    WidgetPointer make_widget(const WidgetContext&) override;
};

#endif //PROBE_WIDGET_H