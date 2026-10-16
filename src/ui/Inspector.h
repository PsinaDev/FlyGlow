#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QToolButton;

namespace fg {

class ParamPanel;
class Project;
class SliderRow;

class Inspector : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(Project* project, QWidget* parent = nullptr);
    /** Updates the area button without emitting maskPaintToggled(). */
    void setMaskPainting(bool on);

signals:
    void maskPaintToggled(bool on);
    void shaderEditRequested();

protected:
    void changeEvent(QEvent* e) override;

private:
    void retranslateUi();
    void rebuildList();
    void refreshRow(int layerIndex);
    void syncCurrent();
    int rowToLayer(int row) const;
    int layerToRow(int layer) const;

    Project* m_project;
    QLabel* m_layersTitle;
    QListWidget* m_list;
    QToolButton* m_addBtn;
    QToolButton* m_dupBtn;
    QToolButton* m_delBtn;
    QToolButton* m_upBtn;
    QToolButton* m_downBtn;

    QWidget* m_layerBox;
    QLabel* m_layerTitle;
    QLineEdit* m_name;
    QLabel* m_blendLabel;
    QComboBox* m_blend;
    QLabel* m_opacityLabel;
    SliderRow* m_opacity;
    QLabel* m_maskLabel;
    QToolButton* m_maskPaint;
    QToolButton* m_maskClear;
    QToolButton* m_maskInvert;
    QLabel* m_maskState;

    QLabel* m_effectTitle;
    QLabel* m_effectHint;
    QPushButton* m_shaderEdit;
    ParamPanel* m_params;
    bool m_syncing = false;
};

} // namespace fg
