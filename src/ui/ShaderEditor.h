#pragma once

#include <QDialog>
#include <QTimer>

class QAction;
class QLabel;
class QListWidget;
class QPushButton;

namespace fg {

class CodeEdit;
class Project;
class ShaderEffect;

/**
 * Edits the current layer's shader. Code that compiles goes into the layer after a short pause in typing;
 * while it has errors the last good version keeps playing.
 */
class ShaderEditor : public QDialog {
    Q_OBJECT
public:
    explicit ShaderEditor(Project* project, QWidget* parent = nullptr);

protected:
    void changeEvent(QEvent* e) override;

private:
    void retranslateUi();
    ShaderEffect* currentShader() const;
    void syncFromProject();
    void apply();
    void showStatus();
    /** Replaces the code as one undoable edit and applies it at once. */
    void loadSource(const QString& source);
    void openFile();
    void saveFile();

    Project* m_project;
    QLabel* m_help;
    QPushButton* m_presetsBtn;
    QList<QAction*> m_presetActions;
    QPushButton* m_openBtn;
    QPushButton* m_saveBtn;
    CodeEdit* m_code;
    QLabel* m_status;
    QListWidget* m_issues;
    QPushButton* m_closeBtn;
    QTimer m_debounce;
    bool m_loading = false;
    bool m_compiled = false;
    bool m_glMissing = false;
};

} // namespace fg
