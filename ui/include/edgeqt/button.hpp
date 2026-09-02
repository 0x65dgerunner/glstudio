#pragma once

#include <QPushButton>

namespace edgeqt {

class Button : public QPushButton {
    Q_OBJECT

public:
    enum class Variant { Default, Outline, Secondary, Ghost, Destructive, Link };
    enum class Size { Xs, Sm, Default, Lg, Icon, IconSm, IconLg };

    explicit Button(QWidget* parent = nullptr);
    explicit Button(const QString& text, QWidget* parent = nullptr);

    void setVariant(Variant variant);
    Variant variant() const { return variant_; }

    void setSize(Size size);
    Size size() const { return size_; }

    void setLeadingIcon(const QString& resourcePath);
    void setIconOnly(const QString& resourcePath);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;

private:
    void applyCursor();
    QSize dimensions() const;
    int radius() const;

    Variant variant_ = Variant::Default;
    Size size_ = Size::Default;
    bool hovered_ = false;
};

}  // namespace edgeqt
