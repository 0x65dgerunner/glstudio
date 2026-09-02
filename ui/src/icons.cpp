#include <edgeqt/icons.hpp>
#include <edgeqt/theme.hpp>

#include <QAbstractButton>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QSvgRenderer>

namespace edgeqt {
namespace {

class HoverIconFilter : public QObject {
public:
    HoverIconFilter(QAbstractButton* button, QIcon strokeIcon, QIcon solidIcon)
        : QObject(button), button_(button), strokeIcon_(std::move(strokeIcon)),
          solidIcon_(std::move(solidIcon)) {
        button_->installEventFilter(this);
    }

    void setIcons(QIcon strokeIcon, QIcon solidIcon) {
        strokeIcon_ = std::move(strokeIcon);
        solidIcon_ = std::move(solidIcon);
        if (button_ != nullptr && button_->underMouse()) {
            button_->setIcon(solidIcon_);
        } else if (button_ != nullptr) {
            button_->setIcon(strokeIcon_);
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched != button_ || !button_->isEnabled()) {
            return QObject::eventFilter(watched, event);
        }
        switch (event->type()) {
            case QEvent::Enter:
                button_->setIcon(solidIcon_);
                break;
            case QEvent::Leave:
                if (!button_->isDown()) {
                    button_->setIcon(strokeIcon_);
                }
                break;
            case QEvent::MouseButtonRelease: {
                const auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (!button_->rect().contains(
                        button_->mapFromGlobal(mouseEvent->globalPosition().toPoint()))) {
                    button_->setIcon(strokeIcon_);
                }
                break;
            }
            default:
                break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QAbstractButton* button_ = nullptr;
    QIcon strokeIcon_;
    QIcon solidIcon_;
};

QHash<QString, QByteArray> g_svgRaw;
QHash<QString, QByteArray> g_blenderSvgs;
bool g_blenderLoaded = false;
QString g_searchPath;

QByteArray loadBlenderCatalog() {
    QFile qrc(QStringLiteral(":/icons/blender.json"));
    if (qrc.open(QIODevice::ReadOnly)) {
        return qrc.readAll();
    }
    if (!g_searchPath.isEmpty()) {
        QDir dir(g_searchPath);
        const QStringList candidates = {
            dir.filePath(QStringLiteral("blender.json")),
            QDir(dir.absoluteFilePath(QStringLiteral("../.."))).filePath(QStringLiteral("icons/blender.json")),
        };
        for (const QString& path : candidates) {
            QFile disk(path);
            if (disk.open(QIODevice::ReadOnly)) {
                return disk.readAll();
            }
        }
    }
    return {};
}

void ensureBlenderSvgs() {
    if (g_blenderLoaded) {
        return;
    }
    g_blenderLoaded = true;
    const QJsonDocument doc = QJsonDocument::fromJson(loadBlenderCatalog());
    if (!doc.isArray()) {
        return;
    }
    const QJsonArray icons = doc.array();
    g_blenderSvgs.reserve(icons.size());
    for (const QJsonValue& value : icons) {
        const QJsonObject obj = value.toObject();
        const QString name = obj.value(QStringLiteral("name")).toString();
        const QString svg = obj.value(QStringLiteral("svg")).toString();
        if (!name.isEmpty() && !svg.isEmpty()) {
            g_blenderSvgs.insert(name, svg.toUtf8());
        }
    }
}

QByteArray loadSvgRaw(const QString& resourcePath) {
    if (resourcePath.startsWith(QLatin1String("blender:"))) {
        ensureBlenderSvgs();
        return g_blenderSvgs.value(resourcePath.mid(8));
    }
    const auto cached = g_svgRaw.constFind(resourcePath);
    if (cached != g_svgRaw.cend()) {
        return cached.value();
    }
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray data = file.readAll();
    g_svgRaw.insert(resourcePath, data);
    return data;
}

QByteArray loadSvg(const QString& resourcePath, const QColor& color) {
    QByteArray data = loadSvgRaw(resourcePath);
    if (color.isValid() && !data.isEmpty()) {
        const QByteArray hex = color.name(QColor::HexRgb).toUtf8();
        const QByteArray fill = QByteArray("fill=\"") + hex + '"';
        data.replace("currentColor", hex);
        data.replace("fill=\"#ffffff\"", fill);
        data.replace("fill=\"#FFFFFF\"", fill);
        data.replace("fill=\"#fff\"", fill);
        data.replace("fill=\"#FFF\"", fill);
        data.replace("fill:#ffffff", QByteArray("fill:") + hex);
        data.replace("fill:#FFFFFF", QByteArray("fill:") + hex);
        data.replace("fill:#fff", QByteArray("fill:") + hex);
        data.replace("fill:#FFF", QByteArray("fill:") + hex);
    }
    return data;
}

QHash<QString, QPixmap> g_pixmaps;

QPixmap renderSvg(const QString& resourcePath, int logicalSize, const QColor& color,
                  qreal devicePixelRatio) {
    const qreal dpr = qMax(devicePixelRatio, 1.0);
    const int physicalSize = qMax(1, qRound(logicalSize * dpr));
    const QString key = resourcePath + QLatin1Char('|') + QString::number(logicalSize) +
                        QLatin1Char('|') + color.name(QColor::HexArgb) + QLatin1Char('|') +
                        QString::number(dpr, 'f', 2);
    const auto cached = g_pixmaps.constFind(key);
    if (cached != g_pixmaps.cend()) {
        return cached.value();
    }

    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.fill(Qt::transparent);

    const QByteArray svg = loadSvg(resourcePath, color);
    if (svg.isEmpty()) {
        return pixmap;
    }

    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        return pixmap;
    }

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, QRectF(0, 0, physicalSize, physicalSize));
    pixmap.setDevicePixelRatio(dpr);
    g_pixmaps.insert(key, pixmap);
    return pixmap;
}

QString resolveFile(const QString& style, const QString& name) {
    const QString qrc = QStringLiteral(":/icons/%1/%2.svg").arg(style, name);
    if (QFile::exists(qrc)) {
        return qrc;
    }
    if (!g_searchPath.isEmpty()) {
        const QString disk = QDir(g_searchPath).filePath(style + QLatin1Char('/') + name +
                                                         QStringLiteral(".svg"));
        if (QFile::exists(disk)) {
            return disk;
        }
    }
    return qrc;
}

QByteArray loadCatalogJson() {
    QFile qrc(QStringLiteral(":/icons/catalog.json"));
    if (qrc.open(QIODevice::ReadOnly)) {
        return qrc.readAll();
    }
    if (!g_searchPath.isEmpty()) {
        QFile disk(QDir(g_searchPath).filePath(QStringLiteral("catalog.json")));
        if (disk.open(QIODevice::ReadOnly)) {
            return disk.readAll();
        }
    }
    return {};
}

}  // namespace

void Icons::setSearchPath(const QString& dir) {
    g_searchPath = dir;
}

QString Icons::searchPath() {
    return g_searchPath;
}

QStringList Icons::catalog() {
    const QJsonDocument doc = QJsonDocument::fromJson(loadCatalogJson());
    QStringList names;
    if (doc.isObject()) {
        const QJsonArray icons = doc.object().value(QStringLiteral("icons")).toArray();
        names.reserve(icons.size());
        for (const QJsonValue& value : icons) {
            names.append(value.toString());
        }
    } else if (doc.isArray()) {
        const QJsonArray icons = doc.array();
        names.reserve(icons.size());
        for (const QJsonValue& value : icons) {
            names.append(value.toString());
        }
    }
    return names;
}

QPixmap Icons::pixmap(const QString& resourcePath, int size, const QColor& color,
                      qreal devicePixelRatio) {
    return renderSvg(resourcePath, size, color, devicePixelRatio);
}

QIcon Icons::icon(const QString& resourcePath, const QColor& color) {
    QIcon result;
    for (const int size : {14, 16, 18, 20, 24, 32}) {
        result.addPixmap(renderSvg(resourcePath, size, color, 1.0));
    }
    return result;
}

QIcon Icons::hoverIcon(const QString& strokePath, const QString& solidPath, const QColor& color) {
    QIcon result;
    for (const int size : {14, 16, 18, 20, 24, 32}) {
        result.addPixmap(renderSvg(strokePath, size, color, 1.0), QIcon::Normal, QIcon::Off);
        result.addPixmap(renderSvg(solidPath, size, color, 1.0), QIcon::Active, QIcon::Off);
        result.addPixmap(renderSvg(solidPath, size, color, 1.0), QIcon::Selected, QIcon::Off);
    }
    return result;
}

void Icons::applyHoverIcon(QAbstractButton* button, const QString& strokePath,
                           const QString& solidPath, const QColor& color) {
    if (button == nullptr) {
        return;
    }
    const QIcon strokeIcon = icon(strokePath, color);
    const QIcon solidIcon = icon(solidPath, color);
    for (QObject* child : button->children()) {
        if (auto* filter = dynamic_cast<HoverIconFilter*>(child)) {
            filter->setIcons(strokeIcon, solidIcon);
            return;
        }
    }
    button->setIcon(strokeIcon);
    new HoverIconFilter(button, strokeIcon, solidIcon);
}

QString Icons::stroke(const QString& name) {
    return resolveFile(QStringLiteral("stroke"), name);
}

QString Icons::solid(const QString& name) {
    return resolveFile(QStringLiteral("solid"), name);
}

QString Icons::blender(const QString& name) {
    return QStringLiteral("blender:") + name;
}

QString Icons::logo() {
    return QStringLiteral(":/icons/logo.svg");
}

}  // namespace edgeqt
