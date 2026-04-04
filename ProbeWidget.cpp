//
// Created by Hannes Sap on 03/04/2026.
//
#include <qboxlayout.h>
#include <Qpainter>
#include <QTabWidget>

#include "ProbeWidget.moc.h"

NukeWidgetCallback::NukeWidgetCallback(ProbeWidget* widget) : widget_(widget) {}

bool NukeWidgetCallback::operator==(const Knob::NotificationCallbackFunctor& other) const {
    const auto& other_callback = dynamic_cast<const NukeWidgetCallback&>(other);
    return widget_ == other_callback.widget_;
}

int NukeWidgetCallback::operator()(const Knob::NotificationCallbackReason reason) const {
    switch (reason) {
        case Knob::kNotification_Changed:
            return 0;
        case Knob::kNotification_Destroying:
            widget_->destroy();
            return 0;
        case Knob::kNotification_UpdateWidgets:
            widget_->update();
            return 0;
        default:
            return 0;
    }
}

NukeWidgetVisibilityCallback::NukeWidgetVisibilityCallback(ProbeWidget* widget) : widget_(widget) {}

bool NukeWidgetVisibilityCallback::operator==(const Knob::VisibilityCallbackFunctor& other) const {
    const auto& other_callback = dynamic_cast<const NukeWidgetVisibilityCallback&>(other);
    return widget_ == other_callback.widget_;
}

int NukeWidgetVisibilityCallback::operator()() const {
    for (QWidget* w = widget_->parentWidget(); w; w = w->parentWidget()) {
        if (qobject_cast<QTabWidget*>(w))
            return widget_->isVisibleTo(w);
    }
    return widget_->isVisible();
}

ProbeGraphic::ProbeGraphic(DummyDeepPixel* deep_pixel, QWidget* parent)
    : QWidget(parent)
    , zoom_(1.0f)
    , deep_pixel_(deep_pixel) {
    this->setMinimumHeight(200);
}

ProbeGraphic::~ProbeGraphic() = default;

void ProbeGraphic::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto rect = QRect(0, 0, width(), height());
    painter.fillRect(rect, Qt::black);

    deep_pixel_->draw(painter, width(), height(), zoom_);
}

void ProbeGraphic::zoom(double zoom) {
    zoom_ = static_cast<float>(zoom);
}

ProbeWidget::ProbeWidget(ProbeKnob* knob, QWidget *parent)
    : QWidget(parent)
      , nuke_knob_(knob)
      , callback_(this)
      , visibility_callback_(this)
{
    nuke_knob_->addNotificationCallback(callback_);
    nuke_knob_->addVisibilityCallback(visibility_callback_);

    graph_ = new ProbeGraphic(nuke_knob_->deep_pixel_, this);

    spin_box_ = new QDoubleSpinBox(this);
    spin_box_->setRange(0.01, 10000.0);
    spin_box_->setValue(1.0);
    spin_box_->setSuffix("x");
    spin_box_->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);

    slider_ = new QSlider(Qt::Horizontal,this);
    slider_->setRange(0, 1000);
    slider_->setValue(500);

    layout_ = new QVBoxLayout(this);
    layout_->addWidget(graph_);
    layout_->addWidget(spin_box_);
    layout_->addWidget(slider_);

    connect(slider_, &QSlider::valueChanged, this, &ProbeWidget::on_slider_moved);
    connect(spin_box_, &QDoubleSpinBox::valueChanged, this, &ProbeWidget::on_slider_moved);
}

ProbeWidget::~ProbeWidget() {
    if (nuke_knob_) {
        nuke_knob_->removeNotificationCallback(callback_);
        nuke_knob_->removeVisibilityCallback(visibility_callback_);
    }
}

void ProbeWidget::on_slider_moved(int value) {
    double log_scale = std::pow(10.0, (value - 500)/250.0);

    spin_box_->blockSignals(true);
    spin_box_->setValue(log_scale);
    spin_box_->blockSignals(false);

    apply_zoom(log_scale);
}

void ProbeWidget::on_spin_changed(double value) {
    int slider_value = static_cast<int>(std::log10(value) * 250 + 500);

    slider_->blockSignals(true);
    slider_->setValue(slider_value);
    slider_->blockSignals(false);

    apply_zoom(slider_value);
}

void ProbeWidget::apply_zoom(double zoom) const {
    graph_->zoom(zoom);
    graph_->update();
}

void ProbeWidget::destroy() {
    nuke_knob_ = nullptr;
}

void ProbeWidget::changed() const {
    //std::cerr << nuke_knob_->deep_pixel_->sample_count << std::endl;
}

void ProbeWidget::update() {
    graph_->update();
    QWidget::update();
}

const ChannelMap DummyDeepPixel::requested_channels = ChannelMap(ChannelSet(Mask_RGBA | Mask_Deep));

DummyDeepPixel::DummyDeepPixel(const DeepPixel& deep_pixel) {
    sample_count = deep_pixel.getSampleCount();
    data = deep_pixel.data();
}

float DummyDeepPixel::get_unordered_sample(size_t sample, Channel channel) const {
    mFnAssert(requested_channels.contains(channel));
    if ( !data || !requested_channels.contains(channel) || channel == Chan_Black) {
        static constexpr float zero = 0.0f;
        return zero;
    }
    return data[sample * requested_channels.size() + requested_channels.chanNo(channel)];
}

void DummyDeepPixel::draw(QPainter& painter, const int width, const int height, const float zoom) {
    for (size_t sample = 0; sample <= sample_count; ++sample) {
        const float red = std::pow(get_unordered_sample(sample, Chan_Red), 1.0/2.2)*255;
        const float green = std::pow(get_unordered_sample(sample, Chan_Green), 1.0/2.2)*255;
        const float blue = std::pow(get_unordered_sample(sample, Chan_Blue), 1.0/2.2)*255;

        const float alpha = get_unordered_sample(sample, Chan_Alpha);
        const float front = get_unordered_sample(sample, Chan_DeepFront)*zoom;
        const float back = get_unordered_sample(sample, Chan_DeepBack)*zoom;

        //std::cout << "Alpha: " << alpha << ", Front: " << front << ", Back: " << back << std::endl;
        painter.setPen(QPen(Qt::green, 2));

        painter.setPen(QPen(QColor(red, green, blue), 2));
        painter.drawLine(front,height, back, (1-alpha)*height);
        //painter.drawRect(front, height, back-front, alpha);
    }
}


ProbeKnob::ProbeKnob(Knob_Closure* kc, DummyDeepPixel* pixel, const char* name)
    : Knob(kc, name), deep_pixel_(pixel) {
}

WidgetPointer ProbeKnob::make_widget(const WidgetContext& context) {
    auto* widget = new ProbeWidget(this);
    return widget;
}
