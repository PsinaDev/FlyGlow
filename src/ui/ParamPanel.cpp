#include "ParamPanel.h"

#include "Controls.h"
#include "Translate.h"
#include "core/Effect.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace fg {

ParamPanel::ParamPanel(QWidget* parent) : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);
}

void ParamPanel::setEffect(Effect* effect)
{
    m_effect = effect;
    rebuild();
}

void ParamPanel::rebuild()
{
    while (QLayoutItem* item = m_layout->takeAt(0)) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }
    if (!m_effect)
        return;

    const QList<ParamDesc> params = m_effect->params();
    if (params.isEmpty()) {
        auto* none = new QLabel(tr("This effect has no settings."), this);
        none->setObjectName("Dim");
        none->setWordWrap(true);
        m_layout->addWidget(none);
        return;
    }

    Effect* e = m_effect;
    for (const ParamDesc& pd : params) {
        const QString id = pd.id;
        const QVariant v = e->value(id);

        if (pd.type == ParamDesc::Type::Bool) {
            auto* row = new QWidget(this);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 6, 0, 6);
            auto* label = new QLabel(trEffect(pd.label), row);
            auto* toggle = new ToggleSwitch(row);
            toggle->setChecked(v.toBool());
            h->addWidget(label, 1);
            h->addWidget(toggle);
            connect(toggle, &QAbstractButton::toggled, this, [this, e, id](bool on) {
                emit editStarted();
                e->setValue(id, on);
                emit changed();
            });
            m_layout->addWidget(row);
            continue;
        }

        auto* label = new QLabel(trEffect(pd.label), this);
        label->setObjectName("Dim");
        label->setContentsMargins(0, 8, 0, 0);
        m_layout->addWidget(label);

        switch (pd.type) {
        case ParamDesc::Type::Float:
        case ParamDesc::Type::Int:
        case ParamDesc::Type::Angle: {
            auto* row = new SliderRow(this);
            double step = pd.step;
            double min = pd.min, max = pd.max;
            QString suffix = pd.suffix;
            if (pd.type == ParamDesc::Type::Int)
                step = std::max(1.0, std::round(pd.step));
            if (pd.type == ParamDesc::Type::Angle) {
                min = 0;
                max = 360;
                step = pd.step >= 1 ? pd.step : 1;
                if (suffix.isEmpty())
                    suffix = "°";
            }
            row->setRange(min, max, step, suffix);
            row->setValue(v.toDouble());
            const bool isInt = pd.type == ParamDesc::Type::Int;
            connect(row, &SliderRow::editStarted, this, &ParamPanel::editStarted);
            connect(row, &SliderRow::valueChanged, this, [this, e, id, isInt](double val) {
                e->setValue(id, isInt ? QVariant(int(std::lround(val))) : QVariant(val));
                emit changed();
            });
            m_layout->addWidget(row);
            break;
        }
        case ParamDesc::Type::Color: {
            auto* btn = new ColorButton(this);
            btn->setColor(v.value<QColor>());
            btn->setMinimumHeight(30);
            connect(btn, &ColorButton::editStarted, this, &ParamPanel::editStarted);
            connect(btn, &ColorButton::colorChanged, this, [this, e, id](const QColor& c) {
                e->setValue(id, c);
                emit changed();
            });
            m_layout->addWidget(btn);
            break;
        }
        case ParamDesc::Type::Gradient: {
            auto* ge = new GradientEditor(this);
            ge->setGradient(ColorGradient::fromVariant(v));
            connect(ge, &GradientEditor::editStarted, this, &ParamPanel::editStarted);
            connect(ge, &GradientEditor::gradientChanged, this, [this, e, id](const ColorGradient& g) {
                e->setValue(id, QVariant::fromValue(g));
                emit changed();
            });
            m_layout->addWidget(ge);
            break;
        }
        case ParamDesc::Type::Choice: {
            auto* combo = new QComboBox(this);
            combo->addItems(trEffects(pd.choices));
            combo->setCurrentIndex(v.toInt());
            connect(combo, &QComboBox::currentIndexChanged, this, [this, e, id](int idx) {
                emit editStarted();
                e->setValue(id, idx);
                emit changed();
            });
            m_layout->addWidget(combo);
            break;
        }
        default:
            break;
        }
    }
}

} // namespace fg
