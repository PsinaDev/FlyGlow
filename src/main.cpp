#include "core/Effect.h"
#include "core/KeyframeEffect.h"
#include "core/MediaEffect.h"
#include "shader/ShaderEffect.h"
#include "shader/ShaderRenderer.h"
#include "ui/Language.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("FlyGlow");
    QApplication::setOrganizationName("FlyGlow");
    QApplication::setApplicationVersion(FLYGLOW_VERSION);
    // The .ico has hand-tuned small sizes; the SVG blurs below 32 px.
    QApplication::setWindowIcon(QIcon(":/ui/flyglow.ico"));
    if (QFileInfo::exists(QCoreApplication::applicationDirPath() + "/portable.txt")) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QCoreApplication::applicationDirPath() + "/data");
    }

    fg::installLanguage(QSettings().value("ui/language", "en").toString());
    fg::registerBuiltinEffects();
    fg::EffectRegistry::instance().add<fg::KeyframeEffect>();
    fg::EffectRegistry::instance().add<fg::MediaEffect>();
    fg::EffectRegistry::instance().add<fg::ShaderEffect>();
    fg::theme::apply(app);

    // Its GL context has to go before the application object, and after every window that renders shaders.
    fg::ShaderRenderer shaderRenderer;
    fg::MainWindow w;
    w.show();
    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        w.openPath(args.at(1));
    return app.exec();
}
