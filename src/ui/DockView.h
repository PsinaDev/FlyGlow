#pragma once

#include "core/Color.h"

#include <QImage>
#include <QTransform>
#include <QVector>
#include <QWidget>
#include <vector>

namespace fg {

class DockView : public QWidget {
    Q_OBJECT
public:
    enum class Tool { None, Brush, Eraser, Fill, Picker, MaskPaint, MaskErase };

    explicit DockView(QWidget* parent = nullptr);

    void setColors(const std::vector<Rgb>& linear);
    const std::vector<Rgb>& colors() const { return m_colors; }

    void setTool(Tool t);
    Tool tool() const { return m_tool; }
    /** Brush size in hex rings around the LED under the cursor: 1 paints exactly that LED. */
    void setBrushSize(int rings);
    int brushSize() const { return m_brushSize; }
    /** 0..1; at 1 every LED in the footprint gets full weight. */
    void setBrushHardness(double h);
    /** Brush and mask strokes also land on the mirror image across the dock's vertical axis. */
    void setMirror(bool on);
    /** Not owned: must stay valid until replaced. nullptr hides the overlay. */
    void setMaskOverlay(const std::vector<float>* mask);
    void setHint(const QString& text);

    QSize sizeHint() const override { return {720, 620}; }
    QSize minimumSizeHint() const override { return {320, 300}; }

signals:
    /** A left-button stroke paints, a right-button stroke erases, whatever tool is selected. */
    void strokeBegan(bool erase);
    /** Weights only grow within a stroke; each is the strongest the LED has received since strokeBegan(). */
    void painted(const QVector<int>& leds, const QVector<float>& weights);
    void strokeEnded();
    void fillAt(int led);
    void pickAt(int led);
    void hoveredLed(int led);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    void updateTransform();
    QPointF toLayout(const QPointF& widgetPos) const;
    void footprint(int centerLed, QVector<int>& leds, QVector<float>& weights) const;
    void applyBrushAt(int centerLed);
    bool isPaintTool() const;

    std::vector<Rgb> m_colors;
    const std::vector<float>* m_mask = nullptr;
    Tool m_tool = Tool::None;
    int m_brushSize = 1;
    double m_hardness = 1.0;
    bool m_mirror = false;
    std::vector<float> m_strokeWeights;
    int m_lastCenter = -1;
    QString m_hint;

    QTransform m_layoutToWidget;
    double m_scale = 1;
    QImage m_glow;

    bool m_stroking = false;
    Qt::MouseButton m_strokeButton = Qt::NoButton;
    QPointF m_lastLayoutPos;
    QPointF m_mouse;
    bool m_mouseInside = false;
    int m_hover = -1;
};

} // namespace fg
