#include "Project.h"

#include "Translate.h"
#include "core/Effect.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>

namespace fg {

namespace {
constexpr size_t kUndoLimit = 200;
}

Project::Project(QObject* parent) : QObject(parent) {}

void Project::setCurrentIndex(int i)
{
    i = layerCount() == 0 ? -1 : std::clamp(i, 0, layerCount() - 1);
    if (i == m_current)
        return;
    m_current = i;
    emit currentChanged(i);
}

int Project::addLayer(std::unique_ptr<Layer> layer, int at)
{
    if (at < 0 || at > layerCount())
        at = layerCount();
    m_scene.layers.insert(m_scene.layers.begin() + at, std::move(layer));
    m_current = at;
    notifyLayersChanged();
    emit currentChanged(m_current);
    return at;
}

int Project::addEffectLayer(const QString& typeId)
{
    auto effect = EffectRegistry::instance().create(typeId);
    if (!effect)
        return -1;
    auto layer = std::make_unique<Layer>();
    layer->name = trEffect(effect->name());
    // Overlay effects fade out through alpha; Screen keeps them bright over a lit layer instead of veiling it.
    for (const ParamDesc& p : effect->params())
        if (p.id == "transparent") {
            layer->blend = BlendMode::Screen;
            break;
        }
    layer->effect = std::move(effect);
    return addLayer(std::move(layer));
}

void Project::removeLayer(int i)
{
    if (!layer(i))
        return;
    m_scene.layers.erase(m_scene.layers.begin() + i);
    m_current = layerCount() == 0 ? -1 : std::min(i, layerCount() - 1);
    notifyLayersChanged();
    emit currentChanged(m_current);
}

void Project::moveLayer(int from, int to)
{
    if (!layer(from) || to < 0 || to >= layerCount() || from == to)
        return;
    auto l = std::move(m_scene.layers[size_t(from)]);
    m_scene.layers.erase(m_scene.layers.begin() + from);
    m_scene.layers.insert(m_scene.layers.begin() + to, std::move(l));
    m_current = to;
    notifyLayersChanged();
    emit currentChanged(m_current);
}

int Project::duplicateLayer(int i)
{
    Layer* src = layer(i);
    if (!src)
        return -1;
    auto copy = src->clone();
    copy->name = tr("%1 (copy)").arg(src->name);
    return addLayer(std::move(copy), i + 1);
}

Project::Snapshot Project::snapshot() const
{
    return {m_scene.toJson(), m_current};
}

void Project::restore(const Snapshot& s)
{
    m_scene.fromJson(s.scene);
    m_current = layerCount() == 0 ? -1 : std::clamp(s.current, 0, layerCount() - 1);
    setModified(true);
    emit layersChanged();
    emit settingsChanged();
    emit currentChanged(m_current);
    emit undoStateChanged();
}

void Project::checkpoint()
{
    m_undo.push_back(snapshot());
    if (m_undo.size() > kUndoLimit)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
    emit undoStateChanged();
}

void Project::undo()
{
    if (m_undo.empty())
        return;
    m_redo.push_back(snapshot());
    Snapshot s = std::move(m_undo.back());
    m_undo.pop_back();
    restore(s);
}

void Project::redo()
{
    if (m_redo.empty())
        return;
    m_undo.push_back(snapshot());
    Snapshot s = std::move(m_redo.back());
    m_redo.pop_back();
    restore(s);
}

void Project::notifyLayerChanged(int i)
{
    setModified(true);
    emit layerChanged(i);
}

void Project::notifyLayersChanged()
{
    setModified(true);
    emit layersChanged();
}

void Project::notifySettingsChanged()
{
    setModified(true);
    emit settingsChanged();
}

void Project::setModified(bool m)
{
    if (m == m_modified)
        return;
    m_modified = m;
    emit modifiedChanged(m);
}

bool Project::save(const QString& path, QString* error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(m_scene.toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    m_path = path;
    setModified(false);
    return true;
}

bool Project::load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull()) {
        if (error)
            *error = pe.errorString();
        return false;
    }
    if (!loadJson(doc.object(), error))
        return false;
    m_path = path;
    setModified(false);
    return true;
}

bool Project::loadJson(const QJsonObject& o, QString* error)
{
    Scene probe;
    if (!probe.fromJson(o, error))
        return false;
    m_scene.fromJson(o);
    m_undo.clear();
    m_redo.clear();
    m_current = layerCount() - 1;
    m_path.clear();
    setModified(false);
    emit layersChanged();
    emit settingsChanged();
    emit currentChanged(m_current);
    emit undoStateChanged();
    return true;
}

void Project::newDefault()
{
    m_scene.layers.clear();
    m_scene.loopSeconds = 6.0;
    m_scene.brightness = 100;
    m_scene.outputGamma = 1.0;
    m_scene.dither = true;
    const auto& entries = EffectRegistry::instance().entries();
    QString base = "aurora";
    if (!EffectRegistry::instance().create(base))
        base = entries.isEmpty() ? QString() : entries.front().typeId;
    m_undo.clear();
    m_redo.clear();
    m_path.clear();
    m_current = -1;
    if (!base.isEmpty()) {
        auto effect = EffectRegistry::instance().create(base);
        auto l = std::make_unique<Layer>();
        l->name = trEffect(effect->name());
        l->effect = std::move(effect);
        m_scene.layers.push_back(std::move(l));
        m_current = 0;
    }
    setModified(false);
    emit layersChanged();
    emit settingsChanged();
    emit currentChanged(m_current);
    emit undoStateChanged();
}

} // namespace fg
