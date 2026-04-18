//
// Created by Hannes Sap on 03/04/2026.
//
#include <QBoxLayout>
#include <Qpainter>
#include <QTabWidget>
#include "DDImage/LUT.h"

#include "ProbeWidget.moc.h"

NukeWidgetCallback::NukeWidgetCallback(ProbeWidget* widget) : widget_(widget) {}

bool NukeWidgetCallback::operator==(const Knob::NotificationCallbackFunctor& other) const {
    const auto& other_callback = dynamic_cast<const NukeWidgetCallback&>(other);
    return widget_ == other_callback.widget_;
}

int NukeWidgetCallback::operator()(const Knob::NotificationCallbackReason reason) const {
    switch (reason) {
        case Knob::kNotification_Changed:
            widget_->changed();
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

    deep_pixel_->draw(painter, static_cast<float>(width()), static_cast<float>(height()), zoom_);
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

void ProbeWidget::on_slider_moved(const int value) const {
    double log_scale = std::pow(10.0, (value - 500)/250.0);

    spin_box_->blockSignals(true);
    spin_box_->setValue(log_scale);
    spin_box_->blockSignals(false);

    apply_zoom(log_scale);
}

void ProbeWidget::on_spin_changed(const double value) const {
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
    graph_->update();
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

void DummyDeepPixel::draw(QPainter& painter, const float width, const float height, const float zoom) const {
    const LUT* monitor_lut = LUT::GetBuiltinLUT(0);

    auto point_list = QList<QPointF>();

    for (size_t sample = 0; sample <= sample_count; ++sample) {
        const float alpha = get_unordered_sample(sample, Chan_Alpha);
        const float front = get_unordered_sample(sample, Chan_DeepFront)*zoom;
        const float back = get_unordered_sample(sample, Chan_DeepBack)*zoom;

        const float red = monitor_lut->to_byte(get_unordered_sample(sample, Chan_Red)/alpha);
        const float green = monitor_lut->to_byte(get_unordered_sample(sample, Chan_Green)/alpha);
        const float blue = monitor_lut->to_byte(get_unordered_sample(sample, Chan_Blue)/alpha);

        // 0,0 coordinate is topLeft
        auto rect = QRectF(
            QPointF(front, (1-alpha)*height),
            QPointF(back, height)
        );

        point_list << rect.topLeft();

        painter.setPen(QPen(QColor(red, green, blue), 1.0, Qt::SolidLine));
        painter.setBrush(QBrush(QColor(red, green, blue)));
        painter.drawRect(rect);
    }

    std::sort(point_list.begin(), point_list.end(), [](const QPointF& a, const QPointF& b) {
        if ( a.x() != b.x() )
            return a.x() < b.x();
        return a.y() < b.y();
    });

    painter.setPen(QPen(Qt::white, 1.0, Qt::DotLine));
    painter.drawPolyline(point_list.data(), point_list.size());
}


ProbeKnob::ProbeKnob(Knob_Closure* kc, DummyDeepPixel* pixel, const char* name)
    : Knob(kc, name), deep_pixel_(pixel) {
}

WidgetPointer ProbeKnob::make_widget(const WidgetContext& context) {
    auto* widget = new ProbeWidget(this);
    return widget;
}
