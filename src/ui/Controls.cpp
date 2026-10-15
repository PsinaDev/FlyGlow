#include "Controls.h"

#include "Theme.h"

#include <QColorDialog>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>

namespace fg {

namespace {

void pickColorLive(QWidget* parent, const QColor& initial, const std::function<void(const QColor&)>& onChange)
{
    QColorDialog dlg(initial, parent);
    dlg.setOption(QColorDialog::DontUseNativeDialog, true);
    dlg.setWindowTitle(ColorButton::tr("Choose a colour"));
    QObject::connect(&dlg, &QColorDialog::currentColorChanged, parent, [&](const QColor& c) {
        if (c.isValid())
            onChange(c);
    });
    if (dlg.exec() == QDialog::Accepted)
        onChange(dlg.selectedColor());
    else
        onChange(initial);
}

} // namespace

void CalmSlider::wheelEvent(QWheelEvent* e)
{
    if (hasFocus())
        QSlider::wheelEvent(e);
    else
        e->ignore();
}

ColorButton::ColorButton(QWidget* parent) : QToolButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    retranslateUi();
    connect(this, &QToolButton::clicked, this, &ColorButton::pick);
}

void ColorButton::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QToolButton::changeEvent(e);
}

void ColorButton::retranslateUi()
{
    setToolTip(tr("Click to choose a colour"));
    setAccessibleName(toolTip());
}

void ColorButton::setColor(const QColor& c)
{
    m_color = c;
    update();
}

void ColorButton::pick()
{
    emit editStarted();
    pickColorLive(this, m_color, [this](const QColor& c) {
        if (c == m_color)
            return;
        m_color = c;
        update();
        emit colorChanged(c);
    });
}

void ColorButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    p.setPen(QPen(underMouse() ? QColor(0x4a, 0x50, 0x60) : theme::kBorder, 1));
    p.setBrush(m_color);
    p.drawRoundedRect(r, 7, 7);
}

void ProgressButton::setProgress(double fraction)
{
    m_progress = fraction < 0 ? -1.0 : std::clamp(fraction, 0.0, 1.0);
    update();
}

void ProgressButton::paintEvent(QPaintEvent* e)
{
    if (m_progress < 0) {
        QPushButton::paintEvent(e);
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath shape;
    shape.addRoundedRect(r, 8, 8);
    p.fillPath(shape, QColor(0x1d, 0x22, 0x30));

    const QRectF filled(r.left(), r.top(), r.width() * m_progress, r.height());
    QLinearGradient fill(r.topLeft(), r.bottomRight());
    fill.setColorAt(0, theme::kAccent);
    fill.setColorAt(1, theme::kAccent2);
    p.save();
    p.setClipRect(filled);
    p.fillPath(shape, fill);
    p.restore();

    // The label changes colour where the fill passes under it.
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    p.save();
    p.setClipRect(filled);
    p.setPen(QColor(0x06, 0x10, 0x18));
    p.drawText(r, Qt::AlignCenter, text());
    p.restore();
    p.save();
    p.setClipRect(QRectF(filled.right(), r.top(), r.right() - filled.right(), r.height()));
    p.setPen(theme::kText);
    p.drawText(r, Qt::AlignCenter, text());
    p.restore();
}

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(1, (height() - 20) / 2.0, 38, 20);
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? theme::kAccent : QColor(0x2a, 0x2f, 0x3b));
    p.drawRoundedRect(r, 10, 10);
    const double x = isChecked() ? r.right() - 10 : r.left() + 10;
    p.setBrush(isChecked() ? QColor(0x06, 0x10, 0x18) : theme::kText);
    p.drawEllipse(QPointF(x, r.center().y()), 7, 7);
}

SliderRow::SliderRow(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);
    m_slider = new CalmSlider(Qt::Horizontal, this);
    m_slider->setFocusPolicy(Qt::StrongFocus);
    m_spin = new QDoubleSpinBox(this);
    m_spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spin->setKeyboardTracking(false);
    m_spin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_spin->setFixedWidth(72);
    lay->addWidget(m_slider, 1);
    lay->addWidget(m_spin);

    connect(m_slider, &QSlider::sliderPressed, this, [this] {
        m_pressed = true;
        emit editStarted();
    });
    connect(m_slider, &QSlider::sliderReleased, this, [this] { m_pressed = false; });
    connect(m_slider, &QSlider::valueChanged, this, [this](int s) {
        if (m_syncing)
            return;
        const double v = fromSlider(s);
        m_syncing = true;
        m_spin->setValue(v);
        m_syncing = false;
        if (!m_pressed)
            emit editStarted();
        emit valueChanged(v);
    });
    connect(m_spin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (m_syncing)
            return;
        m_syncing = true;
        m_slider->setValue(toSlider(v));
        m_syncing = false;
        emit editStarted();
        emit valueChanged(v);
    });
}

void SliderRow::setRange(double min, double max, double step, const QString& suffix)
{
    m_min = min;
    m_max = max;
    m_step = step > 0 ? step : (max - min) / 100.0;
    m_syncing = true;
    m_slider->setRange(0, std::max(1, int(std::lround((max - min) / m_step))));
    int decimals = 0;
    for (double s = m_step; decimals < 4 && std::abs(s - std::round(s)) > 1e-9; s *= 10)
        ++decimals;
    m_spin->setDecimals(decimals);
    m_spin->setRange(min, max);
    m_spin->setSingleStep(m_step);
    m_spin->setSuffix(suffix.isEmpty() ? QString() : " " + suffix);
    m_syncing = false;
}

void SliderRow::setValue(double v)
{
    m_syncing = true;
    m_spin->setValue(v);
    m_slider->setValue(toSlider(v));
    m_syncing = false;
}

double SliderRow::value() const
{
    return m_spin->value();
}

int SliderRow::toSlider(double v) const
{
    return int(std::lround((std::clamp(v, m_min, m_max) - m_min) / m_step));
}

double SliderRow::fromSlider(int s) const
{
    return std::clamp(m_min + s * m_step, m_min, m_max);
}

GradientEditor::GradientEditor(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    retranslateUi();
}

void GradientEditor::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(e);
}

void GradientEditor::retranslateUi()
{
    setToolTip(tr("Click the bar to add a colour · double-click a marker to change it\n"
                  "Drag to move · right-click to delete or pick a preset"));
}

QList<QPair<QString, ColorGradient>> GradientEditor::presets()
{
    return {
        {tr("Rainbow"), ColorGradient::rainbow()},
        {tr("Ocean"), ColorGradient({QColor(0, 40, 120), QColor(0, 140, 255), QColor(0, 230, 200)})},
        {tr("Sunset"), ColorGradient({QColor(255, 60, 40), QColor(255, 150, 30), QColor(170, 40, 200)})},
        {tr("Fire"), ColorGradient({QColor(10, 0, 0), QColor(200, 20, 0), QColor(255, 120, 0), QColor(255, 220, 90)})},
        {tr("Aurora"), ColorGradient({QColor(20, 255, 140), QColor(0, 200, 255), QColor(150, 60, 255)})},
        {tr("Neon"), ColorGradient({QColor(255, 0, 170), QColor(0, 220, 255)})},
        {tr("Ice"), ColorGradient({QColor(255, 255, 255), QColor(120, 200, 255), QColor(40, 80, 255)})},
        {tr("Forest"), ColorGradient({QColor(10, 80, 20), QColor(60, 200, 60), QColor(200, 255, 100)})},
        {tr("Cherry"), ColorGradient({QColor(255, 30, 80), QColor(255, 140, 180), QColor(140, 0, 60)})},
        {tr("Blue → purple"), ColorGradient({QColor(0, 116, 255), QColor(195, 32, 230)})},
    };
}

void GradientEditor::setGradient(const ColorGradient& g)
{
    m_gradient = g;
    if (m_selected >= g.stops().size())
        m_selected = -1;
    update();
}

QRectF GradientEditor::barRect() const
{
    return QRectF(8, 2, width() - 16, 22);
}

int GradientEditor::stopAt(const QPointF& p) const
{
    const QRectF bar = barRect();
    int best = -1;
    double bestD = 10;
    const auto& stops = m_gradient.stops();
    for (int i = 0; i < stops.size(); ++i) {
        const double x = bar.left() + stops[i].pos * bar.width();
        const double d = std::abs(p.x() - x);
        if (p.y() > bar.bottom() - 2 && d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

void GradientEditor::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF bar = barRect();

    QImage strip(std::max(2, int(bar.width())), 1, QImage::Format_RGB32);
    for (int x = 0; x < strip.width(); ++x)
        strip.setPixelColor(x, 0, color::toQColor(m_gradient.sample(double(x) / (strip.width() - 1))));
    QPainterPath clip;
    clip.addRoundedRect(bar, 6, 6);
    p.save();
    p.setClipPath(clip);
    p.drawImage(bar, strip);
    p.restore();
    p.setPen(QPen(theme::kBorder, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(bar, 6, 6);

    const auto& stops = m_gradient.stops();
    for (int i = 0; i < stops.size(); ++i) {
        const double x = bar.left() + stops[i].pos * bar.width();
        QPainterPath marker;
        marker.moveTo(x, bar.bottom() + 1);
        marker.lineTo(x + 6, bar.bottom() + 7);
        marker.lineTo(x + 6, bar.bottom() + 17);
        marker.lineTo(x - 6, bar.bottom() + 17);
        marker.lineTo(x - 6, bar.bottom() + 7);
        marker.closeSubpath();
        p.setPen(QPen(i == m_selected ? theme::kAccent : QColor(0xc8, 0xcd, 0xd8), i == m_selected ? 2 : 1.2));
        p.setBrush(stops[i].color);
        p.drawPath(marker);
    }
}

void GradientEditor::commit(QVector<GradientStop> stops, bool startEdit)
{
    if (startEdit)
        emit editStarted();
    // setStops() re-sorts, so the selected stop is found again by position and colour.
    QColor selColor;
    double selPos = -1;
    if (m_selected >= 0 && m_selected < stops.size()) {
        selColor = stops[m_selected].color;
        selPos = stops[m_selected].pos;
    }
    m_gradient.setStops(stops);
    if (selPos >= 0) {
        const auto& s = m_gradient.stops();
        for (int i = 0; i < s.size(); ++i)
            if (s[i].pos == selPos && s[i].color == selColor) {
                m_selected = i;
                if (m_dragging >= 0)
                    m_dragging = i;
                break;
            }
    }
    update();
    emit gradientChanged(m_gradient);
}

void GradientEditor::mousePressEvent(QMouseEvent* e)
{
    const int s = stopAt(e->position());
    if (e->button() != Qt::LeftButton)
        return;
    if (s >= 0) {
        m_selected = s;
        m_dragging = s;
        m_dragStarted = false;
        update();
        return;
    }
    const QRectF bar = barRect();
    if (bar.adjusted(0, -2, 0, 4).contains(e->position())) {
        const double pos = std::clamp((e->position().x() - bar.left()) / bar.width(), 0.0, 1.0);
        QVector<GradientStop> stops = m_gradient.stops();
        stops.push_back({pos, color::toQColor(m_gradient.sample(pos))});
        m_selected = int(stops.size()) - 1;
        commit(stops, true);
    }
}

void GradientEditor::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging < 0 || !(e->buttons() & Qt::LeftButton)) {
        setCursor(stopAt(e->position()) >= 0 ? Qt::SizeHorCursor : Qt::PointingHandCursor);
        return;
    }
    const QRectF bar = barRect();
    QVector<GradientStop> stops = m_gradient.stops();
    if (m_dragging >= stops.size())
        return;
    stops[m_dragging].pos = std::clamp((e->position().x() - bar.left()) / bar.width(), 0.0, 1.0);
    m_selected = m_dragging;
    commit(stops, !m_dragStarted);
    m_dragStarted = true;
}

void GradientEditor::mouseReleaseEvent(QMouseEvent*)
{
    m_dragging = -1;
    m_dragStarted = false;
}

void GradientEditor::mouseDoubleClickEvent(QMouseEvent* e)
{
    const int s = stopAt(e->position());
    if (s >= 0)
        editStopColor(s);
}

void GradientEditor::editStopColor(int index)
{
    m_selected = index;
    emit editStarted();
    const QColor initial = m_gradient.stops().value(index).color;
    const double pos = m_gradient.stops().value(index).pos;
    pickColorLive(this, initial, [this, index, pos](const QColor& c) {
        QVector<GradientStop> stops = m_gradient.stops();
        if (index >= stops.size())
            return;
        stops[index] = {pos, c};
        m_selected = index;
        commit(stops, false);
    });
}

void GradientEditor::contextMenuEvent(QContextMenuEvent* e)
{
    const int s = stopAt(e->pos());
    QMenu menu(this);
    if (s >= 0) {
        menu.addAction(tr("Change colour…"), this, [this, s] { editStopColor(s); });
        QAction* del = menu.addAction(tr("Delete colour"), this, [this, s] {
            QVector<GradientStop> stops = m_gradient.stops();
            stops.removeAt(s);
            m_selected = -1;
            commit(stops, true);
        });
        del->setEnabled(m_gradient.stops().size() > 1);
        menu.addSeparator();
    }
    menu.addAction(tr("Reverse"), this, [this] {
        QVector<GradientStop> stops = m_gradient.stops();
        for (auto& st : stops)
            st.pos = 1.0 - st.pos;
        m_selected = -1;
        commit(stops, true);
    });
    menu.addAction(tr("Space evenly"), this, [this] {
        QVector<GradientStop> stops = m_gradient.stops();
        for (int i = 0; i < stops.size(); ++i)
            stops[i].pos = stops.size() > 1 ? double(i) / (stops.size() - 1) : 0.0;
        commit(stops, true);
    });
    QMenu* pm = menu.addMenu(tr("Presets"));
    for (const auto& [name, grad] : presets()) {
        const ColorGradient g = grad;
        pm->addAction(name, this, [this, g] {
            m_selected = -1;
            commit(g.stops(), true);
        });
    }
    menu.exec(e->globalPos());
}

} // namespace fg
