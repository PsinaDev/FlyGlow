#include "Timeline.h"

#include "Theme.h"

#include <QEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace fg {

Timeline::Timeline(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(64);
}

void Timeline::changeEvent(QEvent* e)
{
    // Labels are drawn with tr() on every paint.
    if (e->type() == QEvent::LanguageChange)
        update();
    QWidget::changeEvent(e);
}

void Timeline::setLoopSeconds(double s)
{
    m_loopSeconds = std::max(0.1, s);
    update();
}

void Timeline::setPhase(double phase)
{
    m_phase = phase - std::floor(phase);
    update();
}

void Timeline::setKeys(const QVector<double>& times, int selected)
{
    m_keys = times;
    m_selected = selected;
    if (m_drag == Drag::Key)
        m_dragKey = selected;
    update();
}

QRectF Timeline::track() const
{
    return QRectF(14, 6, width() - 28, height() - 12);
}

double Timeline::xToPhase(double x) const
{
    const QRectF t = track();
    return std::clamp((x - t.left()) / t.width(), 0.0, 0.9999);
}

double Timeline::phaseToX(double ph) const
{
    const QRectF t = track();
    return t.left() + ph * t.width();
}

int Timeline::keyAtPos(const QPointF& p) const
{
    const QRectF t = track();
    const double keyY = t.bottom() - 14;
    int best = -1;
    double bestD = 9;
    for (int i = 0; i < m_keys.size(); ++i) {
        const double d = std::hypot(phaseToX(m_keys[i]) - p.x(), keyY - p.y());
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

void Timeline::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), theme::kPanel);
    const QRectF t = track();

    p.setPen(Qt::NoPen);
    p.setBrush(theme::kCard);
    p.drawRoundedRect(t, 8, 8);

    QFont f = font();
    f.setPointSizeF(8);
    p.setFont(f);
    const double pxPerSec = t.width() / m_loopSeconds;
    double minor = 0.1;
    while (minor * pxPerSec < 8)
        minor *= 5;
    const int count = int(std::floor(m_loopSeconds / minor + 1e-6));
    for (int i = 0; i <= count; ++i) {
        const double s = i * minor;
        const double x = t.left() + s * pxPerSec;
        const bool isMajor = i % 5 == 0;
        p.setPen(QPen(isMajor ? QColor(0x4a, 0x50, 0x60) : QColor(0x2e, 0x33, 0x3f), 1));
        p.drawLine(QPointF(x, t.top() + 4), QPointF(x, t.top() + (isMajor ? 13 : 8)));
        if (isMajor && x < t.right() - 20) {
            p.setPen(theme::kTextDim);
            p.drawText(QPointF(x + 3, t.top() + 14), tr("%1 s").arg(QLocale().toString(s, 'g', 3)));
        }
    }

    const double keyY = t.bottom() - 14;
    if (!m_keys.isEmpty()) {
        p.setPen(QPen(QColor(0x2e, 0x33, 0x3f), 1, Qt::DashLine));
        p.drawLine(QPointF(t.left() + 6, keyY), QPointF(t.right() - 6, keyY));
        for (int i = 0; i < m_keys.size(); ++i) {
            const QPointF c(phaseToX(m_keys[i]), keyY);
            QPainterPath d;
            const double r = i == m_selected ? 7 : 6;
            d.moveTo(c.x(), c.y() - r);
            d.lineTo(c.x() + r, c.y());
            d.lineTo(c.x(), c.y() + r);
            d.lineTo(c.x() - r, c.y());
            d.closeSubpath();
            p.setPen(QPen(i == m_selected ? QColor(Qt::white) : QColor(0x0b, 0x0c, 0x10), 1.4));
            p.setBrush(i == m_selected ? theme::kAccent : QColor(0xc8, 0xcd, 0xd8));
            p.drawPath(d);
        }
    }

    const double x = phaseToX(m_phase);
    p.setPen(QPen(theme::kAccent, 2));
    p.drawLine(QPointF(x, t.top()), QPointF(x, t.bottom()));
    QPainterPath head;
    head.moveTo(x - 6, t.top());
    head.lineTo(x + 6, t.top());
    head.lineTo(x, t.top() + 7);
    head.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(theme::kAccent);
    p.drawPath(head);
}

void Timeline::mousePressEvent(QMouseEvent* e)
{
    const int k = keyAtPos(e->position());
    if (e->button() == Qt::RightButton) {
        if (k >= 0) {
            emit keySelected(k);
            emit keyContextMenu(k, e->globalPosition().toPoint());
        }
        return;
    }
    if (e->button() != Qt::LeftButton)
        return;
    if (k >= 0) {
        m_drag = Drag::Key;
        m_dragKey = k;
        emit keySelected(k);
        emit keyDragStarted(k);
        return;
    }
    m_drag = Drag::Scrub;
    emit scrubStarted();
    emit phaseScrubbed(xToPhase(e->position().x()));
}

void Timeline::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drag == Drag::Scrub) {
        emit phaseScrubbed(xToPhase(e->position().x()));
    } else if (m_drag == Drag::Key && m_dragKey >= 0) {
        double ph = xToPhase(e->position().x());
        if (!(e->modifiers() & Qt::AltModifier))
            ph = std::round(ph * 100.0) / 100.0;
        emit keyMoved(m_dragKey, std::min(ph, 0.999));
    } else {
        setCursor(keyAtPos(e->position()) >= 0 ? Qt::SizeHorCursor : Qt::ArrowCursor);
    }
}

void Timeline::mouseReleaseEvent(QMouseEvent*)
{
    if (m_drag == Drag::Key)
        emit keyDragFinished();
    m_drag = Drag::None;
    m_dragKey = -1;
}

} // namespace fg
