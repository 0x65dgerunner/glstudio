#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

namespace edgeqt {

class Select : public QWidget {
    Q_OBJECT

public:
    enum class Size { Sm, Default };

    struct Item {
        QString text;
        QString value;
    };

    explicit Select(QWidget* parent = nullptr);

    void addItem(const QString& text, const QString& value = QString());
    void setItems(const QStringList& items);
    void clearItems();

    void setPlaceholder(const QString& text);
    void setCurrentIndex(int index);
    int currentIndex() const { return currentIndex_; }
    QString currentText() const;
    QString currentValue() const;

    void setSize(Size size);
    Size size() const { return size_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void currentIndexChanged(int index);
    void currentTextChanged(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void openPopup();
    int heightForSize() const;

    QVector<Item> items_;
    int currentIndex_ = -1;
    QString placeholder_ = QStringLiteral("Select");
    Size size_ = Size::Default;
    bool hovered_ = false;
};

}  // namespace edgeqt
