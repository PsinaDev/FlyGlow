#include "Language.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace fg {

namespace {

QTranslator* g_app = nullptr;
QTranslator* g_qt = nullptr;

void drop(QTranslator*& t)
{
    if (!t)
        return;
    QCoreApplication::removeTranslator(t);
    delete t;
    t = nullptr;
}

} // namespace

void installLanguage(const QString& language)
{
    QLocale::setDefault(QLocale(language));
    drop(g_app);
    drop(g_qt);
    if (language == QLatin1String("en"))
        return;
    g_app = new QTranslator;
    if (g_app->load(":/i18n/flyglow_" + language + ".qm"))
        QCoreApplication::installTranslator(g_app);
    // Deployed builds carry qtbase_*.qm next to the exe; dev builds only have it in the Qt install.
    g_qt = new QTranslator;
    for (const QString& dir : {QCoreApplication::applicationDirPath() + "/translations",
                               QLibraryInfo::path(QLibraryInfo::TranslationsPath)}) {
        if (g_qt->load("qtbase_" + language, dir)) {
            QCoreApplication::installTranslator(g_qt);
            break;
        }
    }
}

} // namespace fg
