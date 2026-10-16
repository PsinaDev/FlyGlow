#include "DockView.h"

#include "Theme.h"
#include "core/Effect.h"
#include "core/LedLayout.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

namespace fg {

DockView::DockView(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    m_colors.assign(size_t(LedLayout::cd2().count()), Rgb{});
}

void DockView::setColors(const std::vector<Rgb>& linear)
{
    m_colors = linear;
    update();
}

void DockView::setTool(Tool t)
{
    m_tool = t;
    setCursor(t == Tool::None ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

void DockView::setBrushSize(int rings)
{
    m_brushSize = std::clamp(rings, 1, 8);
    update();
}

void DockView::setBrushHardness(double h)
{
    m_hardness = std::clamp(h, 0.0, 1.0);
    update();
}

void DockView::setMirror(bool on)
{
    m_mirror = on;
    update();
}

void DockView::setMaskOverlay(const std::vector<float>* mask)
{
    m_mask = mask;
    update();
}

void DockView::setHint(const QString& text)
{
    if (m_hint.isEmpty() != text.isEmpty()) {
        m_hint = text;
        updateTransform();
    }
    m_hint = text;
    update();
}

void DockView::resizeEvent(QResizeEvent*)
{
    updateTransform();
}

void DockView::updateTransform()
{
    const LedLayout& L = LedLayout::cd2();
    const QRectF ob = L.outline().boundingRect().adjusted(-0.8, -0.8, 0.8, 0.8);
    const double bottomSpace = m_hint.isEmpty() ? 0 : 28;
    const QRectF area = QRectF(rect()).adjusted(24, 24, -24, -24 - bottomSpace);
    m_scale = std::min(area.width() / ob.width(), area.height() / ob.height());
    m_layoutToWidget = QTransform();
    m_layoutToWidget.translate(area.center().x(), area.center().y());
    m_layoutToWidget.scale(m_scale, m_scale);
    m_layoutToWidget.translate(-ob.center().x(), -ob.center().y());
    m_glow = QImage(size() * devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
    m_glow.setDevicePixelRatio(devicePixelRatioF());
}

QPointF DockView::toLayout(const QPointF& widgetPos) const
{
    return m_layoutToWidget.inverted().map(widgetPos);
}

void DockView::paintEvent(QPaintEvent*)
{
    if (m_glow.isNull() || m_glow.size() != size() * devicePixelRatioF())
        updateTransform();

    const LedLayout& L = LedLayout::cd2();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRadialGradient bg(rect().center(), std::max(width(), height()) * 0.75);
    bg.setColorAt(0, QColor(0x15, 0x17, 0x1e));
    bg.setColorAt(1, theme::kWindow);
    p.fillRect(rect(), bg);

    const QPainterPath body = m_layoutToWidget.map(L.outline());
    const QRectF bodyRect = body.boundingRect();

    {
        float r = 0, g = 0, b = 0;
        for (const Rgb& c : m_colors) {
            r += c.r;
            g += c.g;
            b += c.b;
        }
        const float n = float(std::max<size_t>(1, m_colors.size()));
        const QColor avg = color::toQColor(Rgb{r / n, g / n, b / n});
        QRadialGradient amb(bodyRect.center(), bodyRect.width() * 0.85);
        QColor c0 = avg;
        c0.setAlphaF(0.22f);
        QColor c1 = avg;
        c1.setAlphaF(0.f);
        amb.setColorAt(0, c0);
        amb.setColorAt(1, c1);
        p.fillRect(rect(), amb);
    }

    {
        QLinearGradient fill(bodyRect.topLeft(), bodyRect.bottomLeft());
        fill.setColorAt(0, QColor(0x1d, 0x20, 0x29));
        fill.setColorAt(1, QColor(0x0e, 0x10, 0x15));
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPath(body);
    }

    const double pitch = m_scale;
    m_glow.fill(Qt::transparent);
    {
        QPainter g(&m_glow);
        g.setRenderHint(QPainter::Antialiasing);
        g.setCompositionMode(QPainter::CompositionMode_Plus);
        g.setPen(Qt::NoPen);
        const double gr = pitch * 1.35;
        for (const Led& led : L.leds()) {
            const Rgb& lin = m_colors[size_t(led.index)];
            const float lum = std::max({lin.r, lin.g, lin.b});
            if (lum < 0.002f)
                continue;
            const QPointF c = m_layoutToWidget.map(led.pos);
            QColor col = color::toQColor(lin);
            QRadialGradient rg(c, gr);
            QColor a0 = col, a1 = col, a2 = col;
            a0.setAlphaF(0.55f);
            a1.setAlphaF(0.2f);
            a2.setAlphaF(0.f);
            rg.setColorAt(0.0, a0);
            rg.setColorAt(0.35, a1);
            rg.setColorAt(1.0, a2);
            g.setBrush(rg);
            g.drawEllipse(c, gr, gr);
        }
    }
    p.save();
    p.setClipPath(body);
    p.drawImage(QPointF(0, 0), m_glow);
    p.restore();

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x34, 0x39, 0x46), 1.6));
    p.drawPath(body);
    p.setPen(QPen(QColor(255, 255, 255, 14), 1.0));
    p.drawPath(m_layoutToWidget.map(L.outline()).translated(0, 1.5));

    const double coreR = pitch * 0.34;
    for (const Led& led : L.leds()) {
        const QPointF c = m_layoutToWidget.map(led.pos);
        const Rgb& lin = m_colors[size_t(led.index)];
        const QColor col = color::toQColor(lin);
        p.setPen(QPen(QColor(255, 255, 255, 16), 1));
        p.setBrush(QColor(0x14, 0x16, 0x1c));
        p.drawEllipse(c, coreR, coreR);
        if (std::max({lin.r, lin.g, lin.b}) > 0.002f) {
            p.setPen(Qt::NoPen);
            QRadialGradient core(c, coreR);
            QColor hot = col.lighter(118);
            core.setColorAt(0, hot);
            core.setColorAt(0.75, col);
            QColor edge = col;
            edge.setAlphaF(0.85f);
            core.setColorAt(1, edge);
            p.setBrush(core);
            p.drawEllipse(c, coreR, coreR);
        }
    }

    if (m_mask && !m_mask->empty()) {
        p.setPen(Qt::NoPen);
        for (const Led& led : L.leds()) {
            const float w = (*m_mask)[size_t(led.index)];
            const QPointF c = m_layoutToWidget.map(led.pos);
            if (w < 0.999f) {
                p.setBrush(QColor(8, 9, 12, int((1.f - w) * 200)));
                p.drawEllipse(c, coreR * 1.05, coreR * 1.05);
            }
            if (w > 0.01f) {
                p.setBrush(Qt::NoBrush);
                QColor ring = theme::kAccent;
                ring.setAlphaF(0.25f + 0.6f * w);
                p.setPen(QPen(ring, 1.4));
                p.drawEllipse(c, coreR * 1.25, coreR * 1.25);
                p.setPen(Qt::NoPen);
            }
        }
    }

    if (m_mouseInside && m_tool != Tool::None && m_hover >= 0) {
        QVector<int> leds{m_hover};
        QVector<float> weights{1.f};
        if (isPaintTool())
            footprint(m_hover, leds, weights);
        const bool mask = m_tool == Tool::MaskPaint || m_tool == Tool::MaskErase;
        p.setBrush(Qt::NoBrush);
        for (int k = 0; k < leds.size(); ++k) {
            QColor ring = mask ? theme::kAccent : QColor(255, 255, 255);
            ring.setAlphaF(0.25f + 0.65f * weights[k]);
            p.setPen(QPen(ring, 1.4));
            p.drawEllipse(m_layoutToWidget.map(L.led(leds[k]).pos), coreR * 1.45, coreR * 1.45);
        }
    }

    if (!m_hint.isEmpty()) {
        p.setPen(theme::kTextDim);
        QFont f = font();
        f.setPointSizeF(9.5);
        p.setFont(f);
        p.drawText(QRectF(0, height() - 40, width(), 30), Qt::AlignHCenter | Qt::AlignVCenter, m_hint);
    }
}

bool DockView::isPaintTool() const
{
    return m_tool == Tool::Brush || m_tool == Tool::Eraser || m_tool == Tool::MaskPaint
           || m_tool == Tool::MaskErase;
}

void DockView::footprint(int centerLed, QVector<int>& leds, QVector<float>& weights) const
{
    leds.clear();
    weights.clear();
    if (centerLed < 0)
        return;
    const LedLayout& L = LedLayout::cd2();
    const QPointF c = L.led(centerLed).pos;
    const QPointF mc = L.led(L.mirrorOf(centerLed)).pos;
    const double reach = m_brushSize - 1;
    const double solid = reach * m_hardness;
    for (const Led& led : L.leds()) {
        // Hex neighbours sit 1.0 apart, so ring k is everything within k + slack.
        double d = std::hypot(led.pos.x() - c.x(), led.pos.y() - c.y());
        if (m_mirror)
            d = std::min(d, std::hypot(led.pos.x() - mc.x(), led.pos.y() - mc.y()));
        if (d > reach + 0.2)
            continue;
        const double w = d <= solid + 0.2 ? 1.0 : 1.0 - fx::smooth(solid, reach + 1.0, d);
        if (w > 0.02) {
            leds.push_back(led.index);
            weights.push_back(float(w));
        }
    }
}

void DockView::applyBrushAt(int centerLed)
{
    if (centerLed < 0 || centerLed == m_lastCenter)
        return;
    m_lastCenter = centerLed;
    QVector<int> leds, changed;
    QVector<float> weights, changedWeights;
    footprint(centerLed, leds, weights);
    for (int k = 0; k < leds.size(); ++k) {
        float& best = m_strokeWeights[size_t(leds[k])];
        if (weights[k] > best) {
            best = weights[k];
            changed.push_back(leds[k]);
            changedWeights.push_back(best);
        }
    }
    if (!changed.isEmpty())
        emit painted(changed, changedWeights);
}

void DockView::mousePressEvent(QMouseEvent* e)
{
    const QPointF lp = toLayout(e->position());
    const int led = LedLayout::cd2().ledAt(lp, 0.75);
    if (m_tool == Tool::None || led < 0 || m_stroking)
        return;
    const bool left = e->button() == Qt::LeftButton;
    const bool right = e->button() == Qt::RightButton;
    if (left && (e->modifiers() & Qt::AltModifier) && m_tool != Tool::MaskPaint && m_tool != Tool::MaskErase) {
        emit pickAt(led);
        return;
    }
    if (left && m_tool == Tool::Fill) {
        emit fillAt(led);
        return;
    }
    if (left && m_tool == Tool::Picker) {
        emit pickAt(led);
        return;
    }
    if (!left && !right)
        return;
    m_stroking = true;
    m_strokeButton = e->button();
    m_lastLayoutPos = lp;
    m_lastCenter = -1;
    m_strokeWeights.assign(size_t(LedLayout::cd2().count()), 0.f);
    emit strokeBegan(right);
    applyBrushAt(led);
}

void DockView::mouseMoveEvent(QMouseEvent* e)
{
    m_mouse = e->position();
    m_mouseInside = true;
    const LedLayout& L = LedLayout::cd2();
    const QPointF lp = toLayout(e->position());
    const int led = L.ledAt(lp, 0.75);
    if (led != m_hover) {
        m_hover = led;
        emit hoveredLed(led);
    }
    if (m_stroking) {
        // Walk the segment so a fast drag still touches every LED it crosses.
        const QPointF d = lp - m_lastLayoutPos;
        const int steps = std::max(1, int(std::ceil(std::hypot(d.x(), d.y()) / 0.25)));
        for (int s = 1; s <= steps; ++s)
            applyBrushAt(L.ledAt(m_lastLayoutPos + d * (double(s) / steps), 0.75));
        m_lastLayoutPos = lp;
    }
    update();
}

void DockView::mouseReleaseEvent(QMouseEvent* e)
{
    if (m_stroking && e->button() == m_strokeButton) {
        m_stroking = false;
        emit strokeEnded();
    }
}

void DockView::leaveEvent(QEvent*)
{
    m_mouseInside = false;
    m_hover = -1;
    emit hoveredLed(-1);
    update();
}

} // namespace fg
