#include <edgeqt/fonts.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QPainter>
#include <QSettings>
#include <QWidget>

namespace {

struct LoadedFace {
    QString family;
    bool ok = false;
};

LoadedFace g_geist;
LoadedFace g_mono;
edgeqt::Fonts::Collection g_collection = edgeqt::Fonts::Collection::Geist;

QString keyFor(edgeqt::Fonts::Collection) {
    return QStringLiteral("geist");
}

LoadedFace& faceFor(edgeqt::Fonts::Collection) {
    return g_geist;
}

QString fontsDir() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        const QString candidate = dir.filePath(QStringLiteral("resources/fonts"));
        if (QDir(candidate).exists()) {
            return candidate;
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

bool loadFile(const QString& path, LoadedFace* face) {
    if (path.isEmpty() || !QFile::exists(path)) {
        return false;
    }
    const int id = QFontDatabase::addApplicationFont(path);
    if (id < 0) {
        return false;
    }
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty()) {
        return false;
    }
    if (face->family.isEmpty()) {
        face->family = families.first();
    }
    face->ok = true;
    return true;
}

void loadNamed(LoadedFace* face, const QString& qrcPath, const QString& diskRel) {
    if (loadFile(qrcPath, face)) {
        return;
    }
    const QString root = fontsDir();
    if (!root.isEmpty()) {
        loadFile(QDir(root).filePath(diskRel), face);
    }
}

QFont makeFont(const QString& family, QFont::Weight weight, qreal pointSize) {
    QFont font(family);
    font.setWeight(weight);
    font.setPointSizeF(pointSize);
    edgeqt::Fonts::configure(font);
    return font;
}

bool usesMono(const QFont& font) {
    const QString monoFamily = edgeqt::Fonts::monoFamily();
    if (!monoFamily.isEmpty() &&
        (font.family() == monoFamily || font.families().contains(monoFamily))) {
        return true;
    }
    return font.fixedPitch() || font.styleHint() == QFont::Monospace;
}

void applyFamilyToWidgets(const QString& nextFamily) {
    if (qApp == nullptr || nextFamily.isEmpty()) {
        return;
    }
    QApplication::setFont(edgeqt::Fonts::regular(10.0));

    const auto widgets = QApplication::allWidgets();
    for (QWidget* widget : widgets) {
        QFont font = widget->font();
        if (usesMono(font)) {
            continue;
        }
        font.setFamily(nextFamily);
        edgeqt::Fonts::configure(font);
        widget->setFont(font);
        widget->update();
    }
}

}  // namespace

static void initFontResources() {
    Q_INIT_RESOURCE(resources);
}

namespace edgeqt {

Fonts& Fonts::instance() {
    static Fonts fonts;
    return fonts;
}

Fonts::Fonts(QObject* parent) : QObject(parent) {}

void Fonts::configure(QFont& font) {
    font.setKerning(true);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(static_cast<QFont::StyleStrategy>(
        QFont::PreferOutline | QFont::PreferAntialias | QFont::PreferQuality |
        QFont::NoSubpixelAntialias));
}

void Fonts::preparePainter(QPainter* painter) {
    if (painter == nullptr) {
        return;
    }
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    painter->setRenderHint(QPainter::VerticalSubpixelPositioning, true);
#endif
}

bool Fonts::initialize() {
    initFontResources();

    loadNamed(&g_geist, QStringLiteral(":/fonts/geist/thin.otf"),
              QStringLiteral("Geist/Geist-Thin.otf"));
    loadNamed(&g_geist, QStringLiteral(":/fonts/geist/extralight.otf"),
              QStringLiteral("Geist/Geist-ExtraLight.otf"));
    loadNamed(&g_geist, QStringLiteral(":/fonts/geist/light.otf"),
              QStringLiteral("Geist/Geist-Light.otf"));
    loadNamed(&g_geist, QStringLiteral(":/fonts/geist/regular.otf"),
              QStringLiteral("Geist/Geist-Regular.otf"));
    loadNamed(&g_geist, QStringLiteral(":/fonts/geist/medium.otf"),
              QStringLiteral("Geist/Geist-Medium.otf"));
    loadNamed(&g_geist, QStringLiteral(":/fonts/geist/semibold.otf"),
              QStringLiteral("Geist/Geist-SemiBold.otf"));
    loadNamed(&g_geist, QStringLiteral(":/fonts/geist/bold.otf"),
              QStringLiteral("Geist/Geist-Bold.otf"));
    loadNamed(&g_geist, QStringLiteral(":/fonts/geist/extrabold.otf"),
              QStringLiteral("Geist/Geist-ExtraBold.otf"));
    loadNamed(&g_geist, QStringLiteral(":/fonts/geist/black.otf"),
              QStringLiteral("Geist/Geist-Black.otf"));

    loadNamed(&g_mono, QStringLiteral(":/fonts/geistmono/regular.otf"),
              QStringLiteral("GeistMono/GeistMono-Regular.otf"));
    loadNamed(&g_mono, QStringLiteral(":/fonts/geistmono/medium.otf"),
              QStringLiteral("GeistMono/GeistMono-Medium.otf"));
    loadNamed(&g_mono, QStringLiteral(":/fonts/geistmono/semibold.otf"),
              QStringLiteral("GeistMono/GeistMono-SemiBold.otf"));
    loadNamed(&g_mono, QStringLiteral(":/fonts/geistmono/bold.otf"),
              QStringLiteral("GeistMono/GeistMono-Bold.otf"));

    g_collection = Collection::Geist;

    if (qApp != nullptr) {
        QApplication::setFont(regular(10.0));
    }
    return g_geist.ok;
}

Fonts::Collection Fonts::collection() {
    return g_collection;
}

bool Fonts::isAvailable(Collection collection) {
    return faceFor(collection).ok;
}

void Fonts::setCollection(Collection collection) {
    if (!faceFor(collection).ok) {
        return;
    }
    if (g_collection == collection && !family().isEmpty()) {
        instance().publish();
        return;
    }
    g_collection = collection;
    QSettings settings;
    settings.setValue(QStringLiteral("fontCollection"), keyFor(collection));
    applyFamilyToWidgets(family());
    instance().publish();
}

QString Fonts::collectionLabel(Collection) {
    return QStringLiteral("Geist");
}

QString Fonts::family() {
    return g_geist.ok ? g_geist.family : QString();
}

QString Fonts::monoFamily() {
    return g_mono.ok ? g_mono.family : QString();
}

QFont Fonts::regular(qreal pointSize) {
    return makeFont(family(), QFont::Normal, pointSize);
}

QFont Fonts::medium(qreal pointSize) {
    return makeFont(family(), QFont::Medium, pointSize);
}

QFont Fonts::weight(QFont::Weight weight, qreal pointSize) {
    return makeFont(family(), weight, pointSize);
}

QFont Fonts::mono(qreal pointSize) {
    QFont font;
    if (g_mono.ok) {
        font.setFamily(g_mono.family);
    } else {
        font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Cascadia Code"),
                          QStringLiteral("Consolas"), QStringLiteral("Courier New")});
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
    }
    font.setPointSizeF(pointSize);
    configure(font);
    return font;
}

void Fonts::publish() {
    emit changed();
}

}  // namespace edgeqt
