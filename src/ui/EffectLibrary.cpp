#include "EffectLibrary.h"

#include "Icons.h"
#include "Theme.h"
#include "Thumbnail.h"
#include "Translate.h"
#include "core/Effect.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

namespace fg {

namespace {
constexpr int kTypeRole = Qt::UserRole + 1;
constexpr int kCategoryRole = Qt::UserRole + 2;
constexpr int kSourceTextRole = Qt::UserRole + 3;
}

EffectLibrary::EffectLibrary(QWidget* parent) : QWidget(parent)
{
    setObjectName("Panel");
    setAttribute(Qt::WA_StyledBackground);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(12, 8, 8, 12);
    v->setSpacing(6);

    m_title = new QLabel(this);
    m_title->setObjectName("SectionTitle");
    v->addWidget(m_title);

    m_search = new QLineEdit(this);
    m_search->setClearButtonEnabled(true);
    v->addWidget(m_search);

    m_list = new QListWidget(this);
    m_list->setIconSize(QSize(46, 42));
    m_list->setSpacing(1);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    v->addWidget(m_list, 1);

    m_hint = new QLabel(this);
    m_hint->setObjectName("Dim");
    m_hint->setAlignment(Qt::AlignCenter);
    v->addWidget(m_hint);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(6);
    m_add = new QPushButton(icons::get(icons::Id::Plus), QString(), this);
    m_replace = new QPushButton(this);
    buttons->addWidget(m_add, 1);
    buttons->addWidget(m_replace, 1);
    v->addLayout(buttons);
    m_import = new QPushButton(icons::get(icons::Id::Film), QString(), this);
    v->addWidget(m_import);

    connect(m_import, &QPushButton::clicked, this, &EffectLibrary::importRequested);
    connect(m_search, &QLineEdit::textChanged, this, &EffectLibrary::applyFilter);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const QString t = item->data(kTypeRole).toString();
        if (!t.isEmpty())
            emit addRequested(t);
    });
    connect(m_add, &QPushButton::clicked, this, [this] {
        if (!selectedType().isEmpty())
            emit addRequested(selectedType());
    });
    connect(m_replace, &QPushButton::clicked, this, [this] {
        if (!selectedType().isEmpty())
            emit replaceRequested(selectedType());
    });
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QListWidgetItem* item = m_list->itemAt(pos);
        if (!item || item->data(kTypeRole).toString().isEmpty())
            return;
        const QString t = item->data(kTypeRole).toString();
        QMenu menu(this);
        menu.addAction(tr("Add as a layer"), this, [this, t] { emit addRequested(t); });
        QAction* rep = menu.addAction(tr("Replace in the current layer"), this, [this, t] { emit replaceRequested(t); });
        rep->setEnabled(m_replace->isEnabled());
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    });
    connect(m_list, &QListWidget::currentItemChanged, this, [this] {
        m_add->setEnabled(!selectedType().isEmpty());
    });

    retranslateUi();
}

void EffectLibrary::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(e);
}

void EffectLibrary::retranslateUi()
{
    m_title->setText(tr("Effects").toUpper());
    m_search->setPlaceholderText(tr("Search effects…"));
    m_hint->setText(tr("Double-click to add as a layer"));
    m_add->setText(tr("Add"));
    m_add->setToolTip(tr("Add the selected effect as a new layer on top of the others"));
    m_replace->setText(tr("Replace"));
    m_replace->setToolTip(tr("Put the selected effect into the current layer"));
    m_import->setText(tr("Import GIF / video…"));
    m_import->setToolTip(tr("Map a GIF, picture or video clip onto the dock's LEDs as a new layer"));

    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* item = m_list->item(i);
        const QString text = trEffect(item->data(kSourceTextRole).toString());
        item->setText(item->data(kTypeRole).toString().isEmpty() ? text.toUpper() : text);
    }
    applyFilter(m_search->text());
}

void EffectLibrary::setReplaceEnabled(bool on)
{
    m_replace->setEnabled(on);
}

QString EffectLibrary::selectedType() const
{
    QListWidgetItem* item = m_list->currentItem();
    return item ? item->data(kTypeRole).toString() : QString();
}

void EffectLibrary::reload()
{
    m_list->clear();
    QString lastCategory;
    for (const EffectRegistry::Entry& e : EffectRegistry::instance().entries()) {
        if (!e.listed)
            continue;
        if (e.category != lastCategory) {
            lastCategory = e.category;
            auto* header = new QListWidgetItem(trEffect(e.category).toUpper());
            header->setFlags(Qt::NoItemFlags);
            header->setData(kCategoryRole, e.category);
            header->setData(kSourceTextRole, e.category);
            QFont f = font();
            f.setPointSizeF(8.5);
            f.setBold(true);
            header->setFont(f);
            header->setForeground(theme::kTextDim);
            header->setSizeHint(QSize(10, 30));
            m_list->addItem(header);
        }
        auto effect = EffectRegistry::instance().create(e.typeId);
        auto* item = new QListWidgetItem(trEffect(e.name));
        item->setData(kTypeRole, e.typeId);
        item->setData(kCategoryRole, e.category);
        item->setData(kSourceTextRole, e.name);
        if (effect)
            item->setIcon(QIcon(renderEffectThumbnail(*effect, 0.18, QSize(46, 42))));
        m_list->addItem(item);
    }
    m_add->setEnabled(false);
    applyFilter(m_search->text());
}

void EffectLibrary::applyFilter(const QString& text)
{
    const QString needle = text.trimmed();
    QHash<QString, bool> categoryVisible;
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* item = m_list->item(i);
        if (item->data(kTypeRole).toString().isEmpty())
            continue;
        const bool show = needle.isEmpty() || item->text().contains(needle, Qt::CaseInsensitive);
        item->setHidden(!show);
        if (show)
            categoryVisible[item->data(kCategoryRole).toString()] = true;
    }
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* item = m_list->item(i);
        if (item->data(kTypeRole).toString().isEmpty())
            item->setHidden(!categoryVisible.value(item->data(kCategoryRole).toString()));
    }
}

} // namespace fg
