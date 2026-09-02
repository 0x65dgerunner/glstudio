#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>

class QAbstractButton;

namespace edgeqt {

struct Icons {
    static void setSearchPath(const QString& dir);
    static QString searchPath();
    static QStringList catalog();

    static QIcon icon(const QString& resourcePath, const QColor& color = QColor());
    static QIcon hoverIcon(const QString& strokePath, const QString& solidPath,
                           const QColor& color = QColor());
    static void applyHoverIcon(QAbstractButton* button, const QString& strokePath,
                               const QString& solidPath, const QColor& color = QColor());
    static QPixmap pixmap(const QString& resourcePath, int size, const QColor& color = QColor(),
                          qreal devicePixelRatio = 1.0);

    static QString stroke(const QString& name);
    static QString solid(const QString& name);
    static QString blender(const QString& name);
    static QString logo();
};

}  // namespace edgeqt
