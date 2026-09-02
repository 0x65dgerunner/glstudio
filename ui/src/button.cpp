#include <edgeqt/button.hpp>

#include <edgeqt/fonts.hpp>
#include <edgeqt/theme.hpp>

#include <QEnterEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStyleOptionButton>

namespace edgeqt {
namespace {

QColor mix(const QColor& a, const QColor& b, qreal t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

}  // namespace

Button::Button(QWidget* parent) : QPushButton(parent) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFont(Fonts::medium(9.5));
    setAttribute(Qt::WA_Hover, true);
}

Button::Button(const QString& text, QWidget* parent) : Button(parent) {
    setText(text);
}

void Button::setVariant(Variant variant) {
    variant_ = variant;
    applyCursor();
    update();
}

void Button::setSize(Size size) {
    size_ = size;
    updateGeometry();
    update();
}

void Button::setLeadingIcon(const QString& resourcePath) {
    setIcon(QIcon(resourcePath));
    setIconSize(QSize(14, 14));
}

void Button::setIconOnly(const QString& resourcePath) {
    setIcon(QIcon(resourcePath));
    setText(QString());
    setSize(Size::Icon);
}

void Button::applyCursor() {
    if (variant_ == Variant::Link) {
        setCursor(Qt::PointingHandCursor);
    }
}

QSize Button::dimensions() const {
    switch (size_) {
        case Size::Xs:
            return {0, 24};
        case Size::Sm:
            return {0, 32};
        case Size::Lg:
            return {0, 40};
        case Size::Icon:
            return {36, 36};
        case Size::IconSm:
            return {32, 32};
        case Size::IconLg:
            return {40, 40};
        case Size::Default:
        default:
            return {0, 36};
    }
}

int Button::radius() const {
    if (size_ == Size::Xs) {
        return Theme::radiusSm();
    }
    if (size_ == Size::Sm || size_ == Size::IconSm) {
        return 8;
    }
    return Theme::radiusMd();
}

QSize Button::sizeHint() const {
    const QSize dim = dimensions();
    if (dim.width() > 0) {
        return dim;
    }

    const QFontMetrics metrics(font());
    int width = metrics.horizontalAdvance(text()) + 20;
    if (!icon().isNull()) {
        width += iconSize().width() + 6;
    }
    if (size_ == Size::Lg) {
        width += 4;
    }
    return {qMax(width, 36), dim.height()};
}

QSize Button::minimumSizeHint() const {
    return sizeHint();
}

void Button::enterEvent(QEnterEvent* event) {
    hovered_ = true;
    update();
    QPushButton::enterEvent(event);
}

void Button::leaveEvent(QEvent* event) {
    hovered_ = false;
    update();
    QPushButton::leaveEvent(event);
}

bool Button::event(QEvent* event) {
    if (event->type() == QEvent::EnabledChange) {
        update();
    }
    return QPushButton::event(event);
}

void Button::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    Fonts::preparePainter(&painter);

    QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if (isDown() && variant_ != Variant::Link) {
        bounds.translate(0, 1);
    }

    QColor bg = Qt::transparent;
    QColor fg = Theme::foreground();
    QColor border = Qt::transparent;
    qreal bgAlpha = 1.0;

    switch (variant_) {
        case Variant::Default:
            bg = Theme::primary();
            fg = Theme::primaryForeground();
            if (hovered_) {
                bg = Theme::primaryHover();
            }
            if (isDown()) {
                bg = mix(Theme::primary(), Theme::background(), 0.18);
            }
            break;
        case Variant::Outline:
            bg = hovered_ ? Theme::hover() : Qt::transparent;
            border = Theme::border();
            break;
        case Variant::Secondary:
            bg = Theme::secondary();
            if (hovered_) {
                bg = mix(Theme::secondary(), Theme::foreground(), 0.06);
            }
            break;
        case Variant::Ghost:
            bg = hovered_ ? Theme::hover() : Qt::transparent;
            break;
        case Variant::Destructive:
            bg = Theme::destructive();
            bg.setAlphaF(hovered_ ? 0.22 : 0.14);
            fg = Theme::destructive();
            bgAlpha = 1.0;
            break;
        case Variant::Link:
            fg = hovered_ ? Theme::primaryHover() : Theme::primary();
            break;
    }

    if (!isEnabled()) {
        painter.setOpacity(0.5);
    }

    QPainterPath path;
    path.addRoundedRect(bounds, radius(), radius());

    if (bg.alpha() > 0 && variant_ != Variant::Link) {
        QColor fill = bg;
        if (variant_ != Variant::Destructive) {
            fill.setAlphaF(bgAlpha);
        }
        painter.fillPath(path, fill);
    }

    if (border.alpha() > 0) {
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }

    if (hasFocus()) {
        QColor ring = variant_ == Variant::Destructive ? Theme::destructive() : Theme::ring();
        ring.setAlphaF(0.55);
        painter.setPen(QPen(ring, 1.5));
        painter.drawRoundedRect(bounds.adjusted(1, 1, -1, -1), radius() - 1, radius() - 1);
    }

    QRect textRect = bounds.toRect();
    if (!icon().isNull()) {
        const int iconY = textRect.center().y() - iconSize().height() / 2;
        const int iconX = text().isEmpty() ? textRect.center().x() - iconSize().width() / 2
                                           : textRect.left() + 8;
        icon().paint(&painter, QRect(QPoint(iconX, iconY), iconSize()), Qt::AlignCenter,
                     isEnabled() ? QIcon::Normal : QIcon::Disabled);
        if (!text().isEmpty()) {
            textRect.setLeft(iconX + iconSize().width() + 6);
            textRect.setRight(textRect.right() - 8);
        }
    }

    painter.setPen(fg);
    painter.setFont(font());
    const int flags = Qt::AlignCenter | Qt::TextShowMnemonic;
    if (variant_ == Variant::Link && hovered_) {
        QFont font = this->font();
        font.setUnderline(true);
        painter.setFont(font);
    }
    if (!text().isEmpty()) {
        painter.drawText(textRect, flags, text());
    }
}

}  // namespace edgeqt
