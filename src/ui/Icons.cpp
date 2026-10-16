#include "Icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace fg::icons {

namespace {

// Draws on a 24x24 canvas.
void draw(QPainter& p, Id id, const QColor& c)
{
    QPen pen(c, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (id) {
    case Id::Play: {
        QPainterPath path;
        path.moveTo(8, 5.5);
        path.lineTo(18.5, 12);
        path.lineTo(8, 18.5);
        path.closeSubpath();
        p.setBrush(c);
        p.drawPath(path);
        break;
    }
    case Id::Pause:
        p.setBrush(c);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(7, 5.5, 3.5, 13), 1, 1);
        p.drawRoundedRect(QRectF(13.5, 5.5, 3.5, 13), 1, 1);
        break;
    case Id::Plus:
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Id::Minus:
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Id::Trash:
        p.drawLine(QPointF(5, 7), QPointF(19, 7));
        p.drawLine(QPointF(10, 4.5), QPointF(14, 4.5));
        p.drawRoundedRect(QRectF(7, 7, 10, 12.5), 1.5, 1.5);
        p.drawLine(QPointF(10.5, 10.5), QPointF(10.5, 16));
        p.drawLine(QPointF(13.5, 10.5), QPointF(13.5, 16));
        break;
    case Id::Duplicate:
        p.drawRoundedRect(QRectF(8.5, 8.5, 10.5, 10.5), 2, 2);
        p.drawPolyline(QPolygonF({QPointF(15.5, 5), QPointF(7, 5), QPointF(5, 7), QPointF(5, 15.5)}));
        break;
    case Id::Up:
        p.drawPolyline(QPolygonF({QPointF(6.5, 14.5), QPointF(12, 9), QPointF(17.5, 14.5)}));
        break;
    case Id::Down:
        p.drawPolyline(QPolygonF({QPointF(6.5, 9.5), QPointF(12, 15), QPointF(17.5, 9.5)}));
        break;
    case Id::Eye:
    case Id::EyeOff: {
        QPainterPath path;
        path.moveTo(3.5, 12);
        path.quadTo(12, 3.5, 20.5, 12);
        path.quadTo(12, 20.5, 3.5, 12);
        p.drawPath(path);
        p.drawEllipse(QPointF(12, 12), 2.6, 2.6);
        if (id == Id::EyeOff)
            p.drawLine(QPointF(5, 19), QPointF(19, 5));
        break;
    }
    case Id::Brush: {
        p.drawLine(QPointF(18.5, 5.5), QPointF(10.5, 13.5));
        QPainterPath tip;
        tip.moveTo(10.5, 13.5);
        tip.cubicTo(7, 13, 5.5, 16, 5.5, 19);
        tip.cubicTo(9, 19, 11.5, 17.5, 10.5, 13.5);
        p.setBrush(c);
        p.drawPath(tip);
        break;
    }
    case Id::Eraser: {
        QPainterPath path;
        path.moveTo(13.5, 4.5);
        path.lineTo(19.5, 10.5);
        path.lineTo(11, 19);
        path.lineTo(7, 19);
        path.lineTo(4.5, 16.5);
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(QPointF(9, 9), QPointF(15, 15));
        p.drawLine(QPointF(11, 19), QPointF(19.5, 19));
        break;
    }
    case Id::Fill: {
        QPainterPath bucket;
        bucket.moveTo(11, 4.5);
        bucket.lineTo(18, 11.5);
        bucket.lineTo(11.5, 18);
        bucket.lineTo(4.5, 11);
        bucket.closeSubpath();
        p.drawPath(bucket);
        p.setBrush(c);
        p.drawEllipse(QPointF(19, 17.5), 1.6, 1.6);
        p.drawLine(QPointF(19, 13.5), QPointF(19, 15.5));
        break;
    }
    case Id::Picker:
        p.drawLine(QPointF(5.5, 18.5), QPointF(13, 11));
        p.drawLine(QPointF(11, 9), QPointF(15, 13));
        p.setBrush(c);
        p.drawRoundedRect(QRectF(13.5, 4.5, 6, 6).translated(-0.5, 0.5), 2.5, 2.5);
        break;
    case Id::Mask: {
        p.drawRoundedRect(QRectF(4.5, 4.5, 15, 15), 3, 3);
        QPainterPath half;
        half.addEllipse(QPointF(12, 12), 4.2, 4.2);
        p.setBrush(c);
        p.drawPath(half);
        break;
    }
    case Id::Undo:
    case Id::Redo: {
        p.save();
        if (id == Id::Redo) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        QPainterPath arc;
        arc.moveTo(8, 9.5);
        arc.lineTo(14, 9.5);
        arc.cubicTo(17.5, 9.5, 19.5, 11.8, 19.5, 14.5);
        arc.cubicTo(19.5, 17.2, 17.5, 19, 14, 19);
        arc.lineTo(10, 19);
        p.drawPath(arc);
        p.drawPolyline(QPolygonF({QPointF(11, 6), QPointF(7.5, 9.5), QPointF(11, 13)}));
        p.restore();
        break;
    }
    case Id::Open:
        p.drawPolyline(QPolygonF({QPointF(4.5, 18.5), QPointF(4.5, 6), QPointF(9.5, 6), QPointF(11.5, 8),
                                  QPointF(17.5, 8), QPointF(17.5, 10.5)}));
        p.drawPolygon(QPolygonF({QPointF(4.5, 18.5), QPointF(7.5, 11), QPointF(20.5, 11), QPointF(17.5, 18.5)}));
        break;
    case Id::Save:
        p.drawRoundedRect(QRectF(4.5, 4.5, 15, 15), 2, 2);
        p.drawRect(QRectF(8, 4.5, 8, 5));
        p.drawRoundedRect(QRectF(7.5, 13, 9, 6.5), 1, 1);
        break;
    case Id::New:
        p.drawPolyline(QPolygonF({QPointF(13.5, 4.5), QPointF(6.5, 4.5), QPointF(6.5, 19.5),
                                  QPointF(17.5, 19.5), QPointF(17.5, 8.5), QPointF(13.5, 4.5),
                                  QPointF(13.5, 8.5), QPointF(17.5, 8.5)}));
        break;
    case Id::Send: {
        QPainterPath plane;
        plane.moveTo(4.5, 11.5);
        plane.lineTo(19.5, 4.5);
        plane.lineTo(14.5, 19.5);
        plane.lineTo(11.5, 13);
        plane.closeSubpath();
        p.drawPath(plane);
        p.drawLine(QPointF(11.5, 13), QPointF(19.5, 4.5));
        break;
    }
    case Id::Settings: {
        p.drawEllipse(QPointF(12, 12), 3, 3);
        for (int k = 0; k < 8; ++k) {
            p.save();
            p.translate(12, 12);
            p.rotate(k * 45);
            p.drawLine(QPointF(0, -6.2), QPointF(0, -8.3));
            p.restore();
        }
        p.drawEllipse(QPointF(12, 12), 6.2, 6.2);
        break;
    }
    case Id::KeyAdd:
    case Id::KeyRemove: {
        QPainterPath d;
        d.moveTo(10, 4.5);
        d.lineTo(15.5, 10);
        d.lineTo(10, 15.5);
        d.lineTo(4.5, 10);
        d.closeSubpath();
        p.setBrush(c);
        p.drawPath(d);
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(15, 18), QPointF(21, 18));
        if (id == Id::KeyAdd)
            p.drawLine(QPointF(18, 15), QPointF(18, 21));
        break;
    }
    case Id::Clear:
        p.drawLine(QPointF(7, 7), QPointF(17, 17));
        p.drawLine(QPointF(17, 7), QPointF(7, 17));
        break;
    case Id::Film:
        p.drawRoundedRect(QRectF(4.5, 6, 15, 12), 2, 2);
        for (double x : {7.5, 12.0, 16.5}) {
            p.drawPoint(QPointF(x, 8.5));
            p.drawPoint(QPointF(x, 15.5));
        }
        p.setBrush(c);
        p.drawPolygon(QPolygonF({QPointF(10.5, 10), QPointF(14.5, 12), QPointF(10.5, 14)}));
        break;
    case Id::Dock: {
        QPainterPath w;
        w.moveTo(4, 6);
        w.lineTo(20, 6);
        w.lineTo(12, 20);
        w.closeSubpath();
        p.drawPath(w);
        break;
    }
    case Id::Mirror: {
        p.drawPolygon(QPolygonF({QPointF(9.5, 6.5), QPointF(9.5, 17.5), QPointF(4, 17.5)}));
        p.setBrush(c);
        p.drawPolygon(QPolygonF({QPointF(14.5, 6.5), QPointF(14.5, 17.5), QPointF(20, 17.5)}));
        QPen axis = pen;
        axis.setWidthF(1.4);
        axis.setDashPattern({1.2, 2.0});
        p.setPen(axis);
        p.drawLine(QPointF(12, 3.5), QPointF(12, 20.5));
        break;
    }
    case Id::Code:
        p.drawPolyline(QPolygonF({QPointF(8.5, 7.5), QPointF(4, 12), QPointF(8.5, 16.5)}));
        p.drawPolyline(QPolygonF({QPointF(15.5, 7.5), QPointF(20, 12), QPointF(15.5, 16.5)}));
        p.drawLine(QPointF(13.5, 5.5), QPointF(10.5, 18.5));
        break;
    }
}

} // namespace

QIcon get(Id id, const QColor& color)
{
    QIcon icon;
    for (int scale : {1, 2}) {
        QPixmap pm(24 * scale, 24 * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        draw(p, id, color);
        p.end();
        icon.addPixmap(pm, QIcon::Normal);
        QPixmap dis(pm.size());
        dis.setDevicePixelRatio(scale);
        dis.fill(Qt::transparent);
        QPainter pd(&dis);
        pd.setRenderHint(QPainter::Antialiasing);
        draw(pd, id, QColor(0x55, 0x5b, 0x68));
        pd.end();
        icon.addPixmap(dis, QIcon::Disabled);
    }
    return icon;
}

} // namespace fg::icons
