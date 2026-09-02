#include <edgeqt/input.hpp>

#include <edgeqt/fonts.hpp>
#include <edgeqt/theme.hpp>

#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace edgeqt {

Input::Input(QWidget* parent) : QLineEdit(parent) {
    setFont(Fonts::regular(10.0));
    setFrame(false);
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setMinimumHeight(36);
    setMaximumHeight(36);
    setTextMargins(10, 0, 10, 0);
    Theme::bindStyle(this, []() {
        return QStringLiteral(
                   "QLineEdit {"
                   "  background: transparent;"
                   "  border: none;"
                   "  color: %1;"
                   "  selection-background-color: %2;"
                   "  selection-color: %3;"
                   "  padding: 0;"
                   "}"
                   "QLineEdit::placeholder { color: %4; }")
            .arg(Theme::css(Theme::foreground()), Theme::css(Theme::primary()),
                 Theme::css(Theme::primaryForeground()), Theme::css(Theme::mutedForeground()));
    });
}

void Input::setInvalid(bool invalid) {
    invalid_ = invalid;
    update();
}

QSize Input::sizeHint() const {
    return {200, 36};
}

void Input::focusInEvent(QFocusEvent* event) {
    QLineEdit::focusInEvent(event);
    update();
}

void Input::focusOutEvent(QFocusEvent* event) {
    QLineEdit::focusOutEvent(event);
    update();
}

void Input::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    Fonts::preparePainter(&painter);

    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(bounds, Theme::radiusMd(), Theme::radiusMd());

    QColor fill = Theme::inputFill();
    painter.fillPath(path, fill);

    QColor border = invalid_ ? Theme::destructive() : Theme::border();
    if (hasFocus() && !invalid_) {
        border = Theme::ring();
    }
    painter.setPen(QPen(border, hasFocus() ? 1.4 : 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    if (!isEnabled()) {
        painter.fillPath(path, Theme::overlay());
    }

    QLineEdit::paintEvent(event);
}

}  // namespace edgeqt
