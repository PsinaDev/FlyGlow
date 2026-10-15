#pragma once

#include "core/Scene.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <vector>

namespace fg {

/** Callers edit scene() directly: checkpoint() once per user gesture before it, the matching notify*() after. */
class Project : public QObject {
    Q_OBJECT
public:
    explicit Project(QObject* parent = nullptr);

    Scene& scene() { return m_scene; }
    const Scene& scene() const { return m_scene; }

    int layerCount() const { return int(m_scene.layers.size()); }
    Layer* layer(int i) { return i >= 0 && i < layerCount() ? m_scene.layers[size_t(i)].get() : nullptr; }
    Layer* currentLayer() { return layer(m_current); }
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int i);

    /** Inserts at index at (-1 = on top), makes it current and returns its index. */
    int addLayer(std::unique_ptr<Layer> layer, int at = -1);
    /** Names the layer after the effect in the UI language; returns -1 for an unknown typeId. */
    int addEffectLayer(const QString& typeId);
    void removeLayer(int i);
    void moveLayer(int from, int to);
    int duplicateLayer(int i);

    void checkpoint();
    void undo();
    void redo();
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }

    /** After edits inside one layer: effect params, name, blend, opacity, visibility, mask, keyframes. */
    void notifyLayerChanged(int i);
    /** After layers are added, removed, reordered or get a new effect. */
    void notifyLayersChanged();
    /** After scene-wide edits: loop length, brightness, gamma, dither. */
    void notifySettingsChanged();
    /** After layer names follow a UI language switch; does not mark the document modified. */
    void refreshLayerNames() { emit layersChanged(); }

    QString filePath() const { return m_path; }
    bool isModified() const { return m_modified; }
    bool save(const QString& path, QString* error = nullptr);
    bool load(const QString& path, QString* error = nullptr);
    bool loadJson(const QJsonObject& o, QString* error = nullptr);
    void newDefault();

signals:
    void layersChanged();
    void layerChanged(int index);
    void currentChanged(int index);
    void settingsChanged();
    void modifiedChanged(bool modified);
    void undoStateChanged();

private:
    struct Snapshot {
        QJsonObject scene;
        int current = -1;
    };
    Snapshot snapshot() const;
    void restore(const Snapshot& s);
    void setModified(bool m);

    Scene m_scene;
    int m_current = -1;
    std::vector<Snapshot> m_undo, m_redo;
    QString m_path;
    bool m_modified = false;
};

} // namespace fg
