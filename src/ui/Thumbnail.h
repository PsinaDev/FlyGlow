#pragma once

#include <QPixmap>
#include <vector>

#include "core/Color.h"

namespace fg {

class Effect;

QPixmap renderThumbnail(const std::vector<Rgb>& linear, const QSize& size, qreal dpr = 2.0);
QPixmap renderEffectThumbnail(const Effect& effect, double phase, const QSize& size, qreal dpr = 2.0);

} // namespace fg
