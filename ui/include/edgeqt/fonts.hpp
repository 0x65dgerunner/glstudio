#pragma once

#include <QFont>
#include <QObject>
#include <QString>

class QPainter;

namespace edgeqt {

class Fonts : public QObject {
    Q_OBJECT

public:
    enum class Collection { Geist };

    static Fonts& instance();

    static bool initialize();
    static Collection collection();
    static void setCollection(Collection collection);
    static bool isAvailable(Collection collection);
    static QString collectionLabel(Collection collection = Fonts::collection());
    static QString family();
    static QString monoFamily();
    static QFont regular(qreal pointSize = 10.0);
    static QFont medium(qreal pointSize = 10.0);
    static QFont weight(QFont::Weight weight, qreal pointSize = 10.0);
    static QFont mono(qreal pointSize = 10.0);
    static void configure(QFont& font);
    static void preparePainter(QPainter* painter);

signals:
    void changed();

private:
    explicit Fonts(QObject* parent = nullptr);
    void publish();
};

}  // namespace edgeqt
