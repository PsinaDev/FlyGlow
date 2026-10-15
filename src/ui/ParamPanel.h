#pragma once

#include <QWidget>

class QVBoxLayout;

namespace fg {

class Effect;

class ParamPanel : public QWidget {
    Q_OBJECT
public:
    explicit ParamPanel(QWidget* parent = nullptr);
    /** Rebuilds the editors. Not owned: call setEffect(nullptr) before the effect is destroyed. */
    void setEffect(Effect* effect);
    Effect* effect() const { return m_effect; }

signals:
    void editStarted();
    void changed();

private:
    void rebuild();
    Effect* m_effect = nullptr;
    QVBoxLayout* m_layout;
};

} // namespace fg
