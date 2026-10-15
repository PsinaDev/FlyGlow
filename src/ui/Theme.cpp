#include "Theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace fg::theme {

static QString hex(const QColor& c)
{
    return c.name(QColor::HexRgb);
}

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));

    QFont font("Segoe UI", 10);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);

    QPalette p;
    p.setColor(QPalette::Window, kWindow);
    p.setColor(QPalette::WindowText, kText);
    p.setColor(QPalette::Base, kCard);
    p.setColor(QPalette::AlternateBase, kPanel);
    p.setColor(QPalette::Text, kText);
    p.setColor(QPalette::Button, kCard);
    p.setColor(QPalette::ButtonText, kText);
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, QColor(0x06, 0x0a, 0x10));
    p.setColor(QPalette::ToolTipBase, kCard);
    p.setColor(QPalette::ToolTipText, kText);
    p.setColor(QPalette::PlaceholderText, kTextDim);
    p.setColor(QPalette::Disabled, QPalette::Text, kTextDim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, kTextDim);
    p.setColor(QPalette::Disabled, QPalette::WindowText, kTextDim);
    app.setPalette(p);

    const QString qss = QStringLiteral(R"(
QWidget { color: %TEXT%; }
QMainWindow, #Root { background: %WINDOW%; }
#Panel { background: %PANEL%; border: none; }
#HeaderBar { background: %PANEL%; border-bottom: 1px solid %BORDER%; }
#Stage { background: %WINDOW%; }
#SectionTitle { color: %DIM%; font-size: 8.5pt; font-weight: 600; letter-spacing: 1px; padding: 10px 2px 4px 2px; }
#AppTitle { font-size: 13pt; font-weight: 700; }
#Dim { color: %DIM%; }
#StatusPill { background: %CARD%; border: 1px solid %BORDER%; border-radius: 12px; padding: 3px 12px; color: %DIM%; }

QToolTip { background: %CARD%; color: %TEXT%; border: 1px solid %BORDER%; padding: 5px 8px; border-radius: 6px; }

QPushButton, QToolButton {
    background: %CARD%; border: 1px solid %BORDER%; border-radius: 8px;
    padding: 6px 12px; color: %TEXT%;
}
QToolButton { padding: 5px; }
QPushButton:hover, QToolButton:hover { background: %HOVER%; border-color: #343a48; }
QPushButton:pressed, QToolButton:pressed { background: #2a2f3b; }
QPushButton:disabled, QToolButton:disabled { color: #555b68; background: #15171d; border-color: #1f232b; }
QToolButton:checked { background: rgba(57,208,255,0.16); border-color: %ACCENT%; }
QPushButton#Primary {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 %ACCENT%, stop:1 %ACCENT2%);
    border: none; color: #061018; font-weight: 700; padding: 7px 18px;
}
QPushButton#Primary:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #6fe0ff, stop:1 #a07dff); }
QPushButton#Primary:disabled { background: #1d2230; color: #4f5666; }
QToolButton#Flat, QPushButton#Flat { background: transparent; border: 1px solid transparent; }
QToolButton#Flat:hover, QPushButton#Flat:hover { background: %CARD%; border-color: %BORDER%; }
QToolButton#Flat:checked { background: rgba(57,208,255,0.16); border-color: %ACCENT%; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: %CARD%; border: 1px solid %BORDER%; border-radius: 7px; padding: 4px 8px;
    selection-background-color: %ACCENT%; selection-color: #061018;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: %ACCENT%; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; border: none; }
QComboBox::drop-down { border: none; width: 26px; }
QComboBox::down-arrow { image: url(:/ui/chevron-down.svg); width: 12px; height: 12px; }
QComboBox QAbstractItemView { background: %CARD%; border: 1px solid %BORDER%; selection-background-color: %HOVER%; selection-color: %TEXT%; outline: none; padding: 4px; }

QSlider::groove:horizontal { height: 4px; background: #262a35; border-radius: 2px; }
QSlider::sub-page:horizontal { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %ACCENT2%, stop:1 %ACCENT%); border-radius: 2px; }
QSlider::handle:horizontal { background: %TEXT%; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }
QSlider::handle:horizontal:hover { background: #ffffff; }

QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 34px; height: 18px; border-radius: 9px; background: #262a35; border: 1px solid %BORDER%; image: none; }
QCheckBox::indicator:checked { background: %ACCENT%; border-color: %ACCENT%; }

QListWidget, QTreeWidget { background: transparent; border: none; outline: none; }
QListWidget::item, QTreeWidget::item { border-radius: 8px; padding: 6px; margin: 1px 0; }
QListWidget::item:hover, QTreeWidget::item:hover { background: %CARD%; }
QListWidget::item:selected, QTreeWidget::item:selected { background: %HOVER%; color: %TEXT%; border: 1px solid #343a48; }
QTreeWidget::branch { background: transparent; image: none; border: none; }

QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #2a2f3b; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #3a4150; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #2a2f3b; border-radius: 4px; min-width: 30px; }

QSplitter::handle { background: %BORDER%; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }

QMenu { background: %CARD%; border: 1px solid %BORDER%; border-radius: 8px; padding: 5px; }
QMenu::item { padding: 6px 22px 6px 12px; border-radius: 6px; }
QMenu::item:selected { background: %HOVER%; }
QMenu::separator { height: 1px; background: %BORDER%; margin: 4px 8px; }

QStatusBar { background: %PANEL%; border-top: 1px solid %BORDER%; color: %DIM%; }
QStatusBar::item { border: none; }
QProgressBar { background: #262a35; border: none; border-radius: 3px; height: 6px; text-align: center; color: transparent; }
QProgressBar::chunk { background: %ACCENT%; border-radius: 3px; }
)");
    QString s = qss;
    s.replace("%TEXT%", hex(kText))
        .replace("%WINDOW%", hex(kWindow))
        .replace("%PANEL%", hex(kPanel))
        .replace("%CARD%", hex(kCard))
        .replace("%HOVER%", hex(kCardHover))
        .replace("%BORDER%", hex(kBorder))
        .replace("%DIM%", hex(kTextDim))
        .replace("%ACCENT2%", hex(kAccent2))
        .replace("%ACCENT%", hex(kAccent));
    app.setStyleSheet(s);
}

} // namespace fg::theme
