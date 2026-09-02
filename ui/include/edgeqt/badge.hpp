#pragma once

#include <QWidget>
#include <QString>

namespace edgeqt {

class Badge : public QWidget {
    Q_OBJECT
public:
    enum class Variant { Default, Secondary, Outline, Destructive };
    explicit Badge(const QString& text = {}, QWidget* parent = nullptr);
    void setText(const QString& text);
    void setVariant(Variant variant);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString text_;
    Variant variant_ = Variant::Default;
};

}  // namespace edgeqt
