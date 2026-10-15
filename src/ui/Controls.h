#pragma once

#include "core/Color.h"

#include <QAbstractButton>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QWidget>

class QDoubleSpinBox;

namespace fg {

/** Ignores the mouse wheel unless focused, so scrolling a panel past it leaves the value alone. */
class CalmSlider : public QSlider {
public:
    using QSlider::QSlider;

protected:
    void wheelEvent(QWheelEvent* e) override;
};

/** editStarted() fires on click; colorChanged() then fires live, and once more with the old colour on cancel. */
class ColorButton : public QToolButton {
    Q_OBJECT
public:
    explicit ColorButton(QWidget* parent = nullptr);
    void setColor(const QColor& c);
    QColor color() const { return m_color; }
    QSize sizeHint() const override { return {64, 28}; }

signals:
    void editStarted();
    void colorChanged(const QColor& c);

protected:
    void paintEvent(QPaintEvent*) override;
    void changeEvent(QEvent* e) override;

private:
    void retranslateUi();
    void pick();
    QColor m_color{Qt::white};
};

/** A push button that can fill itself left to right as a progress bar, disabled while it does. */
class ProgressButton : public QPushButton {
    Q_OBJECT
public:
    using QPushButton::QPushButton;
    /** fraction in [0, 1] shows the fill; a negative value restores the normal button. */
    void setProgress(double fraction);
    bool inProgress() const { return m_progress >= 0; }

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    double m_progress = -1;
};

class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {40, 22}; }

protected:
    void paintEvent(QPaintEvent*) override;
};

/** editStarted() fires once per slider drag and before every other user change, ahead of valueChanged(). */
class SliderRow : public QWidget {
    Q_OBJECT
public:
    explicit SliderRow(QWidget* parent = nullptr);
    void setRange(double min, double max, double step, const QString& suffix = {});
    /** Emits nothing. */
    void setValue(double v);
    double value() const;

signals:
    void editStarted();
    void valueChanged(double v);

private:
    int toSlider(double v) const;
    double fromSlider(int s) const;
    CalmSlider* m_slider;
    QDoubleSpinBox* m_spin;
    double m_min = 0, m_max = 1, m_step = 0.01;
    bool m_pressed = false;
    bool m_syncing = false;
};

/** editStarted() fires once per gesture (click, drag, menu action, colour dialog), ahead of gradientChanged(). */
class GradientEditor : public QWidget {
    Q_OBJECT
public:
    explicit GradientEditor(QWidget* parent = nullptr);
    void setGradient(const ColorGradient& g);
    ColorGradient gradient() const { return m_gradient; }
    QSize sizeHint() const override { return {240, 44}; }
    QSize minimumSizeHint() const override { return {120, 44}; }

    static QList<QPair<QString, ColorGradient>> presets();

signals:
    void editStarted();
    void gradientChanged(const ColorGradient& g);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void changeEvent(QEvent* e) override;

private:
    void retranslateUi();
    QRectF barRect() const;
    int stopAt(const QPointF& p) const;
    void editStopColor(int index);
    void commit(QVector<GradientStop> stops, bool startEdit);

    ColorGradient m_gradient;
    int m_selected = -1;
    int m_dragging = -1;
    bool m_dragStarted = false;
};

} // namespace fg
