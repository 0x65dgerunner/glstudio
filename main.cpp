#include "window.hpp"

#include <edgeqt/fonts.hpp>
#include <edgeqt/icons.hpp>
#include <edgeqt/theme.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QStyleFactory>
#include <QSurfaceFormat>

namespace {

QString findIconDir() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        const QString candidate = dir.filePath(QStringLiteral("ui/resources/icons"));
        if (QFile::exists(candidate + QStringLiteral("/catalog.json")) ||
            QFile::exists(candidate + QStringLiteral("/blender.json")) ||
            QDir(dir.filePath(QStringLiteral("ui/resources/icons/stroke"))).exists()) {
            return candidate;
        }
        const QString legacy = dir.filePath(QStringLiteral("resources/icons"));
        if (QFile::exists(legacy + QStringLiteral("/catalog.json")) ||
            QDir(dir.filePath(QStringLiteral("resources/icons/stroke"))).exists()) {
            return legacy;
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

}  // namespace

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    qputenv("QT_QPA_PLATFORM", "windows:fontengine=freetype");
#endif
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
    QSurfaceFormat glFormat;
    glFormat.setDepthBufferSize(24);
    glFormat.setAlphaBufferSize(8);
    glFormat.setSamples(0);
    glFormat.setSwapInterval(1);
    glFormat.setVersion(3, 3);
    glFormat.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(glFormat);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("glstudio"));
    QApplication::setOrganizationName(QStringLiteral("glstudio"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    edgeqt::Fonts::initialize();
    edgeqt::Icons::setSearchPath(findIconDir());

    GlStudioWindow window;
    window.resize(1280, 800);
    window.show();
    return app.exec();
}
