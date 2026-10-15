#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class QLineEdit;
class QPushButton;

namespace fg {

class EffectLibrary : public QWidget {
    Q_OBJECT
public:
    explicit EffectLibrary(QWidget* parent = nullptr);
    void reload();
    void setReplaceEnabled(bool on);

signals:
    void addRequested(const QString& typeId);
    void replaceRequested(const QString& typeId);
    void importRequested();

protected:
    void changeEvent(QEvent* e) override;

private:
    void retranslateUi();
    QString selectedType() const;
    void applyFilter(const QString& text);

    QLabel* m_title;
    QLineEdit* m_search;
    QListWidget* m_list;
    QLabel* m_hint;
    QPushButton* m_add;
    QPushButton* m_replace;
    QPushButton* m_import;
};

} // namespace fg
