#include "LedLayout.h"

#include <QStringList>
#include <QTransform>
#include <cmath>

namespace fg {

namespace {

constexpr double kSvgCentreX = 225.0;
constexpr double kSvgTop = 60.0;
constexpr double kSvgColPitch = 22.0;
constexpr double kSvgRowPitch = 19.0;
constexpr int kRowLengths[] = {14, 15, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3};
constexpr int kRows = int(sizeof(kRowLengths) / sizeof(kRowLengths[0]));

// From Space Station's 450x420 preview SVG, like the constants above; it uses only absolute M, C and Z.
const char* kOutlineSvg =
    "M274.78 356.907C251.909 394.467 197.76 394.668 174.677 357.239C151.446 "
    "319.57 124.535 275.392 103.773 239.748C82.8096 203.758 57.3861 158.047 "
    "35.934 118.974C14.8564 80.5825 41.4546 33.7818 85.2258 32.3413C130.137 "
    "30.8633 182.959 29.5 225 29.5C266.782 29.5 319.212 30.8466 363.942 "
    "32.314C407.917 33.7566 434.505 80.9474 413.108 119.396C391.514 158.197 "
    "366.066 203.469 345.226 239.248C324.478 274.868 297.783 319.13 274.78 "
    "356.907Z";

QPainterPath parseSvgPath(const QString& d)
{
    QPainterPath path;
    QString spaced;
    for (QChar ch : d) {
        if (ch.isLetter()) {
            spaced += ' ';
            spaced += ch;
            spaced += ' ';
        } else {
            spaced += ch;
        }
    }
    const QStringList tok = spaced.split(' ', Qt::SkipEmptyParts);
    QChar cmd;
    int i = 0;
    auto num = [&]() { return tok.value(i++).toDouble(); };
    while (i < tok.size()) {
        if (tok[i].size() == 1 && tok[i][0].isLetter())
            cmd = tok[i++][0];
        if (cmd == 'M') {
            const double x = num(), y = num();
            path.moveTo(x, y);
        } else if (cmd == 'C') {
            const double x1 = num(), y1 = num(), x2 = num(), y2 = num(), x = num(), y = num();
            path.cubicTo(x1, y1, x2, y2, x, y);
        } else if (cmd == 'Z') {
            path.closeSubpath();
        } else {
            ++i;
        }
    }
    return path;
}

} // namespace

const LedLayout& LedLayout::cd2()
{
    static const LedLayout layout;
    return layout;
}

LedLayout::LedLayout()
{
    const double svgCentreY = kSvgTop + kSvgRowPitch * (kRows - 1) / 2.0;
    int index = 0;
    for (int row = 0; row < kRows; ++row) {
        const int len = kRowLengths[row];
        m_rowLengths.push_back(len);
        m_rowStarts.push_back(index);
        for (int col = 0; col < len; ++col) {
            const double sx = kSvgCentreX + kSvgColPitch * (col - (len - 1) / 2.0);
            const double sy = kSvgTop + kSvgRowPitch * row;
            Led led;
            led.index = index++;
            led.row = row;
            led.col = col;
            led.pos = QPointF((sx - kSvgCentreX) / kSvgColPitch, (sy - svgCentreY) / kSvgColPitch);
            m_leds.push_back(led);
        }
    }

    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9, cx = 0, cy = 0;
    for (const Led& l : m_leds) {
        minX = std::min(minX, l.pos.x());
        maxX = std::max(maxX, l.pos.x());
        minY = std::min(minY, l.pos.y());
        maxY = std::max(maxY, l.pos.y());
        cx += l.pos.x();
        cy += l.pos.y();
    }
    m_bounds = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
    m_centroid = QPointF(cx / count(), cy / count());

    const double half = std::max(m_bounds.width(), m_bounds.height()) / 2.0;
    double maxR = 0;
    for (Led& l : m_leds) {
        l.norm = l.pos / half;
        l.uv = QPointF((l.pos.x() - minX) / m_bounds.width(), (l.pos.y() - minY) / m_bounds.height());
        const QPointF d = l.pos - m_centroid;
        l.radius = std::hypot(d.x(), d.y());
        l.angle = std::atan2(d.y(), d.x());
        maxR = std::max(maxR, l.radius);
    }
    for (Led& l : m_leds)
        l.radius /= maxR;

    m_neighbors.resize(m_leds.size());
    for (const Led& a : m_leds)
        for (const Led& b : m_leds)
            if (a.index != b.index && std::hypot(a.pos.x() - b.pos.x(), a.pos.y() - b.pos.y()) < 1.1)
                m_neighbors[size_t(a.index)].push_back(b.index);

    QTransform t;
    t.scale(1.0 / kSvgColPitch, 1.0 / kSvgColPitch);
    t.translate(-kSvgCentreX, -svgCentreY);
    m_outline = t.map(parseSvgPath(QString::fromLatin1(kOutlineSvg)));
}

int LedLayout::ledAt(const QPointF& p, double maxDist) const
{
    int best = -1;
    double bestD = maxDist;
    for (const Led& l : m_leds) {
        const double d = std::hypot(l.pos.x() - p.x(), l.pos.y() - p.y());
        if (d <= bestD) {
            bestD = d;
            best = l.index;
        }
    }
    return best;
}

int LedLayout::mirrorOf(int i) const
{
    const Led& l = led(i);
    return rowStart(l.row) + rowLength(l.row) - 1 - l.col;
}

std::vector<int> LedLayout::ledsWithin(const QPointF& p, double r) const
{
    std::vector<int> out;
    for (const Led& l : m_leds)
        if (std::hypot(l.pos.x() - p.x(), l.pos.y() - p.y()) <= r)
            out.push_back(l.index);
    return out;
}

} // namespace fg
