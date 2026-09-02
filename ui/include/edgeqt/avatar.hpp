#pragma once

#include <QWidget>
#include <QString>
#include <QPixmap>

namespace edgeqt {

class Avatar : public QWidget {
    Q_OBJECT
public:
    explicit Avatar(QWidget* parent = nullptr);
    void setSize(int pixelSize);
    void setInitials(const QString& initials);
    void setImage(const QPixmap& pixmap);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int size_ = 40;
    QString initials_;
    QPixmap image_;
};

}  // namespace edgeqt
