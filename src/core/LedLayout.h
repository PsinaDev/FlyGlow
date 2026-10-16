#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <vector>

namespace fg {

struct Led {
    int index = 0;
    int row = 0;
    int col = 0;          /**< within its row */
    /** Layout units: horizontal LED pitch 1, row pitch 19/22, origin at the LED bounding-box centre, y down. */
    QPointF pos;
    QPointF norm;         /**< pos / 7.5: x in [-1, 1], y in about [-0.86, 0.86] */
    QPointF uv;           /**< 0..1 across the LED bounding box, u left to right, v top to bottom */
    double radius = 0;    /**< distance from centroid(), normalised so the farthest LED is 1 */
    double angle = 0;     /**< atan2 around centroid() in radians; 0 = right, clockwise (y down) */
};

/** The CD2's 162 LEDs in a 16-row hex lattice, row-major from the top-left: the order the dock expects. */
class LedLayout {
public:
    static const LedLayout& cd2();

    int count() const { return int(m_leds.size()); }
    const Led& led(int i) const { return m_leds[size_t(i)]; }
    const std::vector<Led>& leds() const { return m_leds; }

    int rowCount() const { return int(m_rowLengths.size()); }
    int rowLength(int row) const { return m_rowLengths[size_t(row)]; }
    int rowStart(int row) const { return m_rowStarts[size_t(row)]; }

    /** Bounding box of the LED centres, layout units. */
    QRectF bounds() const { return m_bounds; }
    /** Mean LED position. */
    QPointF centroid() const { return m_centroid; }
    double pitch() const { return 1.0; }

    /** Dock silhouette in layout units. */
    const QPainterPath& outline() const { return m_outline; }

    /** Indices of the up to 6 hex neighbours. */
    const std::vector<int>& neighbors(int i) const { return m_neighbors[size_t(i)]; }
    /** The LED in the same row on the other side of the vertical axis. */
    int mirrorOf(int i) const;

    /** Nearest LED to p (layout units), or -1 if none is within maxDist. */
    int ledAt(const QPointF& p, double maxDist = 0.6) const;
    /** All LEDs within r of p, layout units. */
    std::vector<int> ledsWithin(const QPointF& p, double r) const;

private:
    LedLayout();
    std::vector<int> m_rowLengths, m_rowStarts;
    std::vector<Led> m_leds;
    std::vector<std::vector<int>> m_neighbors;
    QRectF m_bounds;
    QPointF m_centroid;
    QPainterPath m_outline;
};

} // namespace fg
