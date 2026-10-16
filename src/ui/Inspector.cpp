#include "Inspector.h"

#include "Controls.h"
#include "Icons.h"
#include "ParamPanel.h"
#include "Project.h"
#include "Theme.h"
#include "Thumbnail.h"
#include "Translate.h"
#include "core/Effect.h"
#include "core/KeyframeEffect.h"
#include "core/LedLayout.h"
#include "shader/ShaderEffect.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>

namespace fg {

namespace {

QToolButton* toolButton(QWidget* parent, icons::Id icon)
{
    auto* b = new QToolButton(parent);
    b->setIcon(icons::get(icon));
    b->setIconSize(QSize(18, 18));
    b->setObjectName("Flat");
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

void setTip(QWidget* w, const QString& tip)
{
    w->setToolTip(tip);
    w->setAccessibleName(tip);
}

QLabel* label(QWidget* parent, const char* objectName, int topMargin = 0)
{
    auto* l = new QLabel(parent);
    l->setObjectName(objectName);
    l->setContentsMargins(0, topMargin, 0, 0);
    return l;
}

class LayerRow : public QWidget {
public:
    LayerRow(QWidget* parent) : QWidget(parent)
    {
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(4, 4, 8, 4);
        h->setSpacing(8);
        eye = new QToolButton(this);
        eye->setObjectName("Flat");
        eye->setIconSize(QSize(16, 16));
        eye->setCursor(Qt::PointingHandCursor);
        eye->setToolTip(Inspector::tr("Show / hide layer"));
        thumb = new QLabel(this);
        thumb->setFixedSize(40, 36);
        auto* texts = new QVBoxLayout();
        texts->setSpacing(0);
        name = new QLabel(this);
        sub = new QLabel(this);
        sub->setObjectName("Dim");
        QFont f = sub->font();
        f.setPointSizeF(8.5);
        sub->setFont(f);
        texts->addWidget(name);
        texts->addWidget(sub);
        h->addWidget(eye);
        h->addWidget(thumb);
        h->addLayout(texts, 1);
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
    }
    QToolButton* eye;
    QLabel* thumb;
    QLabel* name;
    QLabel* sub;
};

} // namespace

Inspector::Inspector(Project* project, QWidget* parent) : QWidget(parent), m_project(project)
{
    setObjectName("Panel");
    setAttribute(Qt::WA_StyledBackground);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 12, 10);
    root->setSpacing(4);

    auto* head = new QHBoxLayout();
    m_layersTitle = label(this, "SectionTitle");
    head->addWidget(m_layersTitle, 1);
    m_addBtn = toolButton(this, icons::Id::Plus);
    m_dupBtn = toolButton(this, icons::Id::Duplicate);
    m_upBtn = toolButton(this, icons::Id::Up);
    m_downBtn = toolButton(this, icons::Id::Down);
    m_delBtn = toolButton(this, icons::Id::Trash);
    for (QToolButton* b : {m_addBtn, m_dupBtn, m_upBtn, m_downBtn, m_delBtn})
        head->addWidget(b);
    root->addLayout(head);

    m_list = new QListWidget(this);
    m_list->setFixedHeight(200);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    root->addWidget(m_list);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll);
    auto* cv = new QVBoxLayout(content);
    cv->setContentsMargins(0, 0, 6, 0);
    cv->setSpacing(4);

    m_layerBox = new QWidget(content);
    auto* lv = new QVBoxLayout(m_layerBox);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(4);
    m_layerTitle = label(m_layerBox, "SectionTitle");
    lv->addWidget(m_layerTitle);
    m_name = new QLineEdit(m_layerBox);
    lv->addWidget(m_name);

    m_blendLabel = label(m_layerBox, "Dim", 8);
    lv->addWidget(m_blendLabel);
    m_blend = new QComboBox(m_layerBox);
    for (int i = 0; i < blendModeNames().size(); ++i)
        m_blend->addItem(QString());
    lv->addWidget(m_blend);

    m_opacityLabel = label(m_layerBox, "Dim", 8);
    lv->addWidget(m_opacityLabel);
    m_opacity = new SliderRow(m_layerBox);
    m_opacity->setRange(0, 100, 1, "%");
    lv->addWidget(m_opacity);

    m_maskLabel = label(m_layerBox, "Dim", 8);
    lv->addWidget(m_maskLabel);
    auto* maskRow = new QHBoxLayout();
    maskRow->setSpacing(6);
    m_maskPaint = new QToolButton(m_layerBox);
    m_maskPaint->setIcon(icons::get(icons::Id::Mask));
    m_maskPaint->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_maskPaint->setCheckable(true);
    m_maskClear = toolButton(m_layerBox, icons::Id::Clear);
    m_maskInvert = new QToolButton(m_layerBox);
    maskRow->addWidget(m_maskPaint, 1);
    maskRow->addWidget(m_maskInvert);
    maskRow->addWidget(m_maskClear);
    lv->addLayout(maskRow);
    m_maskState = new QLabel(m_layerBox);
    m_maskState->setObjectName("Dim");
    lv->addWidget(m_maskState);
    cv->addWidget(m_layerBox);

    m_effectTitle = label(content, "SectionTitle");
    cv->addWidget(m_effectTitle);
    m_effectHint = new QLabel(content);
    m_effectHint->setWordWrap(true);
    m_effectHint->setObjectName("Dim");
    cv->addWidget(m_effectHint);
    m_shaderEdit = new QPushButton(icons::get(icons::Id::Code), QString(), content);
    m_shaderEdit->setVisible(false);
    cv->addWidget(m_shaderEdit);
    m_params = new ParamPanel(content);
    cv->addWidget(m_params);
    cv->addStretch(1);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    auto* addMenu = new QMenu(this);
    connect(addMenu, &QMenu::aboutToShow, this, [this, addMenu] {
        addMenu->clear();
        std::map<QString, QMenu*> cats;
        for (const auto& e : EffectRegistry::instance().entries()) {
            if (!cats.count(e.category))
                cats[e.category] = addMenu->addMenu(trEffect(e.category));
            const QString t = e.typeId;
            cats[e.category]->addAction(trEffect(e.name), this, [this, t] {
                m_project->checkpoint();
                m_project->addEffectLayer(t);
            });
        }
    });
    m_addBtn->setMenu(addMenu);
    m_addBtn->setPopupMode(QToolButton::InstantPopup);

    connect(m_dupBtn, &QToolButton::clicked, this, [this] {
        if (m_project->currentLayer()) {
            m_project->checkpoint();
            m_project->duplicateLayer(m_project->currentIndex());
        }
    });
    connect(m_delBtn, &QToolButton::clicked, this, [this] {
        if (m_project->currentLayer()) {
            m_project->checkpoint();
            m_project->removeLayer(m_project->currentIndex());
        }
    });
    connect(m_upBtn, &QToolButton::clicked, this, [this] {
        const int i = m_project->currentIndex();
        if (i >= 0 && i + 1 < m_project->layerCount()) {
            m_project->checkpoint();
            m_project->moveLayer(i, i + 1);
        }
    });
    connect(m_downBtn, &QToolButton::clicked, this, [this] {
        const int i = m_project->currentIndex();
        if (i > 0) {
            m_project->checkpoint();
            m_project->moveLayer(i, i - 1);
        }
    });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!m_syncing && row >= 0)
            m_project->setCurrentIndex(rowToLayer(row));
    });

    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        Layer* l = m_project->currentLayer();
        if (!l || m_name->text() == l->name)
            return;
        m_project->checkpoint();
        l->name = m_name->text();
        m_project->notifyLayerChanged(m_project->currentIndex());
    });
    connect(m_blend, &QComboBox::currentIndexChanged, this, [this](int idx) {
        Layer* l = m_project->currentLayer();
        if (m_syncing || !l)
            return;
        m_project->checkpoint();
        l->blend = BlendMode(idx);
        m_project->notifyLayerChanged(m_project->currentIndex());
    });
    connect(m_opacity, &SliderRow::editStarted, m_project, &Project::checkpoint);
    connect(m_opacity, &SliderRow::valueChanged, this, [this](double v) {
        Layer* l = m_project->currentLayer();
        if (m_syncing || !l)
            return;
        l->opacity = v / 100.0;
        m_project->notifyLayerChanged(m_project->currentIndex());
    });
    connect(m_maskPaint, &QToolButton::toggled, this, &Inspector::maskPaintToggled);
    connect(m_shaderEdit, &QPushButton::clicked, this, &Inspector::shaderEditRequested);
    connect(m_maskClear, &QToolButton::clicked, this, [this] {
        Layer* l = m_project->currentLayer();
        if (!l || l->mask.empty())
            return;
        m_project->checkpoint();
        l->mask.clear();
        m_project->notifyLayerChanged(m_project->currentIndex());
        syncCurrent();
    });
    connect(m_maskInvert, &QToolButton::clicked, this, [this] {
        Layer* l = m_project->currentLayer();
        if (!l)
            return;
        m_project->checkpoint();
        // An empty mask means the whole dock, so its inverse covers nothing.
        if (l->mask.empty())
            l->mask.assign(size_t(LedLayout::cd2().count()), 0.f);
        else
            for (float& w : l->mask)
                w = 1.f - w;
        m_project->notifyLayerChanged(m_project->currentIndex());
        syncCurrent();
    });

    connect(m_params, &ParamPanel::editStarted, m_project, &Project::checkpoint);
    connect(m_params, &ParamPanel::changed, this, [this] {
        m_project->notifyLayerChanged(m_project->currentIndex());
    });

    connect(m_project, &Project::layersChanged, this, [this] {
        rebuildList();
        syncCurrent();
    });
    connect(m_project, &Project::currentChanged, this, [this] { syncCurrent(); });
    connect(m_project, &Project::layerChanged, this, [this](int i) {
        refreshRow(i);
        if (i == m_project->currentIndex()) {
            Layer* l = m_project->currentLayer();
            const int n = l && !l->mask.empty()
                              ? int(std::count_if(l->mask.begin(), l->mask.end(), [](float w) { return w > 0.5f; }))
                              : -1;
            m_maskState->setText(n < 0 ? tr("The layer affects the whole dock")
                                       : tr("The layer affects %1 of 162 LEDs").arg(n));
        }
    });

    retranslateUi();
}

void Inspector::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(e);
}

void Inspector::retranslateUi()
{
    m_layersTitle->setText(tr("Layers").toUpper());
    setTip(m_addBtn, tr("Add layer"));
    setTip(m_dupBtn, tr("Duplicate layer"));
    setTip(m_upBtn, tr("Move layer up"));
    setTip(m_downBtn, tr("Move layer down"));
    setTip(m_delBtn, tr("Delete layer"));

    m_layerTitle->setText(tr("Layer").toUpper());
    m_name->setPlaceholderText(tr("Layer name"));
    m_blendLabel->setText(tr("Blend mode"));
    const QStringList blends = trEffects(blendModeNames());
    for (int i = 0; i < blends.size(); ++i)
        m_blend->setItemText(i, blends[i]);
    m_opacityLabel->setText(tr("Opacity"));
    m_maskLabel->setText(tr("Area"));
    m_maskPaint->setText(tr("Paint area"));
    m_maskPaint->setToolTip(tr("Brush over the LEDs this layer should affect.\n"
                               "The eraser in the tool strip removes them from the area."));
    setTip(m_maskClear, tr("Whole dock (reset the area)"));
    m_maskInvert->setText(tr("Invert"));
    m_maskInvert->setToolTip(tr("Invert the area"));
    m_shaderEdit->setText(tr("Edit shader code…"));

    rebuildList();
    syncCurrent();
}

void Inspector::setMaskPainting(bool on)
{
    const QSignalBlocker block(m_maskPaint);
    m_maskPaint->setChecked(on);
}

int Inspector::rowToLayer(int row) const
{
    return m_project->layerCount() - 1 - row;
}

int Inspector::layerToRow(int layer) const
{
    return m_project->layerCount() - 1 - layer;
}

void Inspector::rebuildList()
{
    m_syncing = true;
    m_list->clear();
    for (int row = 0; row < m_project->layerCount(); ++row) {
        const int li = rowToLayer(row);
        auto* item = new QListWidgetItem();
        item->setSizeHint(QSize(10, 48));
        m_list->addItem(item);
        auto* w = new LayerRow(m_list);
        m_list->setItemWidget(item, w);
        connect(w->eye, &QToolButton::clicked, this, [this, li] {
            Layer* l = m_project->layer(li);
            if (!l)
                return;
            m_project->checkpoint();
            l->visible = !l->visible;
            m_project->notifyLayerChanged(li);
        });
        refreshRow(li);
    }
    m_list->setCurrentRow(layerToRow(m_project->currentIndex()));
    m_syncing = false;
}

void Inspector::refreshRow(int layerIndex)
{
    Layer* l = m_project->layer(layerIndex);
    QListWidgetItem* item = m_list->item(layerToRow(layerIndex));
    if (!l || !item)
        return;
    auto* w = static_cast<LayerRow*>(m_list->itemWidget(item));
    if (!w)
        return;
    w->eye->setIcon(icons::get(l->visible ? icons::Id::Eye : icons::Id::EyeOff,
                               l->visible ? theme::kText : theme::kTextDim));
    w->name->setText(l->name.isEmpty() ? tr("Layer %1").arg(layerIndex + 1) : l->name);
    QStringList bits;
    if (l->effect)
        bits << trEffect(l->effect->name());
    if (l->blend != BlendMode::Normal)
        bits << trEffect(blendModeNames().value(int(l->blend)));
    if (l->opacity < 0.999)
        bits << QStringLiteral("%1%").arg(int(std::lround(l->opacity * 100)));
    if (!l->mask.empty())
        bits << tr("area");
    w->sub->setText(bits.join(QStringLiteral(" · ")));
    if (l->effect)
        w->thumb->setPixmap(renderEffectThumbnail(*l->effect, 0.18, QSize(40, 36)));
    w->setEnabled(true);
    w->name->setStyleSheet(l->visible ? QString() : QStringLiteral("color: %1;").arg(theme::kTextDim.name()));
}

void Inspector::syncCurrent()
{
    m_syncing = true;
    const int cur = m_project->currentIndex();
    if (m_list->currentRow() != layerToRow(cur))
        m_list->setCurrentRow(layerToRow(cur));
    Layer* l = m_project->currentLayer();
    m_layerBox->setVisible(l != nullptr);
    m_dupBtn->setEnabled(l != nullptr);
    m_delBtn->setEnabled(l != nullptr);
    m_upBtn->setEnabled(l && cur + 1 < m_project->layerCount());
    m_downBtn->setEnabled(l && cur > 0);
    if (l) {
        m_name->setText(l->name);
        m_blend->setCurrentIndex(int(l->blend));
        m_opacity->setValue(l->opacity * 100.0);
        const int n = l->mask.empty()
                          ? -1
                          : int(std::count_if(l->mask.begin(), l->mask.end(), [](float w) { return w > 0.5f; }));
        m_maskState->setText(n < 0 ? tr("The layer affects the whole dock")
                                   : tr("The layer affects %1 of 162 LEDs").arg(n));
        const bool isKeys = dynamic_cast<KeyframeEffect*>(l->effect.get()) != nullptr;
        m_effectTitle->setText(l->effect ? tr("Effect · %1").arg(trEffect(l->effect->name())).toUpper()
                                         : tr("Effect").toUpper());
        m_effectHint->setVisible(isKeys);
        m_effectHint->setText(tr("Paint right on the dock: brush, eraser, fill and eyedropper are in the strip above it.\n"
                                 "Strokes go into the keyframe at the playhead; if there is none, one is created. "
                                 "Colours flow smoothly between keyframes."));
        m_shaderEdit->setVisible(dynamic_cast<ShaderEffect*>(l->effect.get()) != nullptr);
        m_params->setEffect(isKeys ? nullptr : l->effect.get());
    } else {
        m_effectTitle->setText(tr("Add a layer from the effect library on the left"));
        m_effectHint->setVisible(false);
        m_shaderEdit->setVisible(false);
        m_params->setEffect(nullptr);
    }
    m_syncing = false;
}

} // namespace fg
