#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>

namespace fg {

/** Display text for a name, category, label or choice that src/core declares in the "Effects" context. */
inline QString trEffect(const QString& source)
{
    return QCoreApplication::translate("Effects", source.toUtf8().constData());
}

inline QStringList trEffects(const QStringList& sources)
{
    QStringList out;
    out.reserve(sources.size());
    for (const QString& s : sources)
        out << trEffect(s);
    return out;
}

} // namespace fg
