#include <edgeqt/checkbox.hpp>
#include <edgeqt/fonts.hpp>
#include <edgeqt/icons.hpp>
#include <edgeqt/slider.hpp>
#include <edgeqt/textarea.hpp>
#include <edgeqt/theme.hpp>

#include <QAbstractAnimation>
#include <QButtonGroup>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPropertyAnimation>
#include <QRegion>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace edgeqt {

Checkbox::Checkbox(const QString& text, QWidget* parent) : QAbstractButton(parent) {
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFont(Fonts::regular(10.0));
}

QSize Checkbox::sizeHint() const {
    return {20 + QFontMetrics(font()).horizontalAdvance(text()) + 10, 22};
}

void Checkbox::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Fonts::preparePainter(&p);
    const QRect box(0, (height() - 16) / 2, 16, 16);
    QPainterPath path;
    path.addRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    if (isChecked()) {
        p.fillPath(path, Theme::primary());
        const QPixmap check =
            Icons::pixmap(Icons::stroke(QStringLiteral("check")), 12, Theme::primaryForeground(),
                          devicePixelRatioF());
        p.drawPixmap(box.center() - QPoint(6, 6), check);
    } else {
        p.setPen(QPen(Theme::border(), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    p.setPen(Theme::foreground());
    p.setFont(font());
    p.drawText(QRect(22, 0, width() - 22, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
}

Switch::Switch(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(32, 18);
    setAttribute(Qt::WA_Hover, true);
    setAutoFillBackground(false);
}

void Switch::setKnob(qreal value) {
    knob_ = qBound(0.0, value, 1.0);
    update();
}

void Switch::nextCheckState() {
    const qreal from = knob_;
    const qreal to = isChecked() ? 0.0 : 1.0;
    QAbstractButton::nextCheckState();
    auto* anim = new QPropertyAnimation(this, "knob", this);
    anim->setDuration(180);
    anim->setStartValue(from);
    anim->setEndValue(to);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void Switch::checkStateSet() {
    knob_ = isChecked() ? 1.0 : 0.0;
    update();
}

QSize Switch::sizeHint() const { return {32, 18}; }

void Switch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Fonts::preparePainter(&p);
    if (!isEnabled()) {
        p.setOpacity(0.45);
    }

    auto mix = [](const QColor& a, const QColor& b, qreal t) {
        return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                                a.greenF() + (b.greenF() - a.greenF()) * t,
                                a.blueF() + (b.blueF() - a.blueF()) * t,
                                a.alphaF() + (b.alphaF() - a.alphaF()) * t);
    };

    const QRectF bounds = QRectF(rect());
    const qreal radius = bounds.height() * 0.5;
    QPainterPath track;
    track.addRoundedRect(bounds, radius, radius);

    const QColor trackOff = Theme::input();
    const QColor trackOn = Theme::primary();
    p.fillPath(track, mix(trackOff, trackOn, knob_));

    if (knob_ < 1.0) {
        QColor border = Theme::border();
        border.setAlphaF(border.alphaF() * (1.0 - knob_));
        p.setPen(QPen(border, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(track);
    }

    const qreal pad = 2.0;
    const qreal thumb = bounds.height() - pad * 2.0;
    const qreal travel = bounds.width() - pad * 2.0 - thumb;
    const QRectF knobRect(bounds.left() + pad + travel * knob_, bounds.top() + pad, thumb, thumb);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255));
    p.drawEllipse(knobRect);
}

Toggle::Toggle(const QString& text, QWidget* parent) : QAbstractButton(parent) {
    setText(text);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFont(Fonts::medium(9.5));
    setAttribute(Qt::WA_Hover, true);
    connect(&Theme::instance(), &Theme::changed, this, [this]() { update(); });
}

void Toggle::setLeadingIcon(const QString& resourcePath) {
    setLeadingIcon(resourcePath, QString());
}

void Toggle::setLeadingIcon(const QString& strokePath, const QString& solidPath) {
    iconPath_ = strokePath;
    solidIconPath_ = solidPath;
    updateGeometry();
    update();
}

QSize Toggle::sizeHint() const {
    const QFontMetrics metrics(font());
    const int textW = text().isEmpty() ? 0 : metrics.horizontalAdvance(text());
    const int iconW = iconPath_.isEmpty() ? 0 : 16;
    const int gap = (!iconPath_.isEmpty() && !text().isEmpty()) ? 8 : 0;
    return {12 + iconW + gap + textW + 12, 36};
}

void Toggle::enterEvent(QEnterEvent* event) {
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void Toggle::leaveEvent(QEvent* event) {
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void Toggle::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Fonts::preparePainter(&p);
    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(bounds, Theme::radiusMd(), Theme::radiusMd());

    QColor fill = Qt::transparent;
    if (isChecked()) {
        fill = Theme::accent();
    } else if (hovered_) {
        fill = Theme::hover();
    }
    if (fill.alpha() > 0) {
        p.fillPath(path, fill);
    }
    p.setPen(QPen(Theme::input(), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    const int iconSize = 16;
    const int gap = (!iconPath_.isEmpty() && !text().isEmpty()) ? 8 : 0;
    const int textW = text().isEmpty() ? 0 : QFontMetrics(font()).horizontalAdvance(text());
    const int iconW = iconPath_.isEmpty() ? 0 : iconSize;
    const int contentW = iconW + gap + textW;
    int x = (width() - contentW) / 2;
    const int y = (height() - iconSize) / 2;

    if (!iconPath_.isEmpty()) {
        const QString path =
            (isChecked() && !solidIconPath_.isEmpty()) ? solidIconPath_ : iconPath_;
        const QPixmap pix =
            Icons::pixmap(path, iconSize, Theme::foreground(), devicePixelRatioF());
        p.drawPixmap(x, y, pix);
        x += iconSize + gap;
    }

    p.setPen(Theme::foreground());
    p.setFont(font());
    p.drawText(QRect(x, 0, textW, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
}

Radio::Radio(const QString& text, QWidget* parent) : QAbstractButton(parent) {
    setText(text);
    setCheckable(true);
    setAutoExclusive(true);
    setCursor(Qt::PointingHandCursor);
    setFont(Fonts::regular(10.0));
}

QSize Radio::sizeHint() const {
    return {20 + QFontMetrics(font()).horizontalAdvance(text()) + 10, 22};
}

void Radio::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Fonts::preparePainter(&p);
    const QRectF outer(1, (height() - 16) / 2.0, 16, 16);
    p.setPen(QPen(isChecked() ? Theme::primary() : Theme::border(), 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(outer);
    if (isChecked()) {
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::primary());
        p.drawEllipse(outer.adjusted(4, 4, -4, -4));
    }
    p.setPen(Theme::foreground());
    p.setFont(font());
    p.drawText(QRect(22, 0, width() - 22, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
}

RadioGroup::RadioGroup(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
}

Radio* RadioGroup::addItem(const QString& text, const QString& value) {
    auto* radio = new Radio(text, this);
    radio->setProperty("value", value.isEmpty() ? text : value);
    layout()->addWidget(radio);
    radios_.append(radio);
    connect(radio, &Radio::toggled, this, [this, radio](bool on) {
        if (on) {
            emit currentChanged(radio->property("value").toString());
        }
    });
    if (radios_.size() == 1) {
        radio->setChecked(true);
    }
    return radio;
}

QString RadioGroup::currentValue() const {
    for (Radio* radio : radios_) {
        if (radio->isChecked()) {
            return radio->property("value").toString();
        }
    }
    return {};
}

void RadioGroup::setCurrentValue(const QString& value) {
    for (Radio* radio : radios_) {
        radio->setChecked(radio->property("value").toString() == value);
    }
}

Slider::Slider(QWidget* parent) : QWidget(parent) {
    setFixedHeight(24);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
}

void Slider::setRange(int min, int max) {
    min_ = min;
    max_ = max;
    setValue(value_);
}

void Slider::setValue(int value) {
    const int next = qBound(min_, value, max_);
    if (next == value_) {
        return;
    }
    value_ = next;
    update();
    emit valueChanged(value_);
}

QSize Slider::sizeHint() const { return {200, 24}; }

void Slider::setFromPos(int x) {
    const qreal t = qBound(0.0, (x - 8.0) / qMax(1.0, width() - 16.0), 1.0);
    setValue(min_ + qRound(t * (max_ - min_)));
}

void Slider::mousePressEvent(QMouseEvent* event) {
    setFromPos(event->pos().x());
}
void Slider::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        setFromPos(event->pos().x());
    }
}

void Slider::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Fonts::preparePainter(&p);
    const int y = height() / 2;
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::secondary());
    p.drawRoundedRect(QRect(8, y - 2, width() - 16, 4), 2, 2);
    const qreal t = (max_ == min_) ? 0 : (value_ - min_) / qreal(max_ - min_);
    const int filled = qRound((width() - 16) * t);
    p.setBrush(Theme::primary());
    p.drawRoundedRect(QRect(8, y - 2, filled, 4), 2, 2);
    p.setPen(QPen(Theme::background(), 2.0));
    p.setBrush(Theme::foreground());
    p.drawEllipse(QPoint(8 + filled, y), 8, 8);
}

Textarea::Textarea(QWidget* parent) : QTextEdit(parent) {
    setFont(Fonts::regular(10.0));
    setFrameShape(QFrame::NoFrame);
    setAcceptRichText(false);
    setTabChangesFocus(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setMinimumSize(240, 80);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setMouseTracking(true);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    viewport()->setAutoFillBackground(false);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, false);
    document()->setDocumentMargin(0);
    setViewportMargins(14, 12, 14, 14);
    Theme::bindStyle(this, []() {
        return QStringLiteral(
                   "QTextEdit {"
                   "  background: transparent;"
                   "  border: none;"
                   "  color: %1;"
                   "  selection-background-color: %2;"
                   "  selection-color: %3;"
                   "  padding: 0;"
                   "}"
                   "QTextEdit::placeholder { color: %4; }"
                   "QScrollBar:vertical { width: 8px; background: transparent; }"
                   "QScrollBar::handle:vertical { background: %5; border-radius: 4px; min-height: 20px; }"
                   "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }")
            .arg(Theme::css(Theme::foreground()), Theme::css(Theme::primary()),
                 Theme::css(Theme::primaryForeground()), Theme::css(Theme::mutedForeground()),
                 Theme::css(Theme::scrollbar()));
    });
}

void Textarea::applyClip() {
    if (width() <= 0 || height() <= 0) {
        return;
    }
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()), Theme::radiusMd(), Theme::radiusMd());
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}

void Textarea::resizeEvent(QResizeEvent* event) {
    QTextEdit::resizeEvent(event);
    applyClip();
}

void Textarea::setInvalid(bool invalid) {
    invalid_ = invalid;
    update();
}

QSize Textarea::sizeHint() const {
    return preferred_;
}

QSize Textarea::minimumSizeHint() const {
    return {240, 80};
}

QRect Textarea::gripRect() const {
    return {width() - 18, height() - 18, 18, 18};
}

void Textarea::applyUserSize(const QSize& size) {
    const QSize next(qBound(minimumWidth(), size.width(), maximumWidth()),
                     qBound(minimumHeight(), size.height(), maximumHeight()));
    preferred_ = next;
    resize(next);
    updateGeometry();
}

void Textarea::focusInEvent(QFocusEvent* event) {
    QTextEdit::focusInEvent(event);
    update();
}

void Textarea::focusOutEvent(QFocusEvent* event) {
    QTextEdit::focusOutEvent(event);
    update();
}

void Textarea::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && gripRect().contains(event->pos())) {
        resizing_ = true;
        resizeOrigin_ = event->globalPosition().toPoint();
        resizeStart_ = size();
        event->accept();
        return;
    }
    QTextEdit::mousePressEvent(event);
}

void Textarea::mouseMoveEvent(QMouseEvent* event) {
    const bool overGrip = gripRect().contains(event->pos());
    setCursor(overGrip || resizing_ ? Qt::SizeFDiagCursor : Qt::IBeamCursor);
    if (resizing_) {
        const QPoint delta = event->globalPosition().toPoint() - resizeOrigin_;
        applyUserSize(QSize(resizeStart_.width() + delta.x(), resizeStart_.height() + delta.y()));
        event->accept();
        return;
    }
    QTextEdit::mouseMoveEvent(event);
}

void Textarea::mouseReleaseEvent(QMouseEvent* event) {
    if (resizing_ && event->button() == Qt::LeftButton) {
        resizing_ = false;
        setCursor(gripRect().contains(event->pos()) ? Qt::SizeFDiagCursor : Qt::IBeamCursor);
        event->accept();
        return;
    }
    QTextEdit::mouseReleaseEvent(event);
}

void Textarea::leaveEvent(QEvent* event) {
    if (!resizing_) {
        unsetCursor();
    }
    QTextEdit::leaveEvent(event);
}

void Textarea::paintEvent(QPaintEvent* event) {
    QPainter p(this);
    Fonts::preparePainter(&p);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(bounds, Theme::radiusMd(), Theme::radiusMd());

    p.fillPath(path, Theme::muted());

    QColor border = invalid_ ? Theme::destructive() : Theme::border();
    if (hasFocus() && !invalid_) {
        border = Theme::ring();
    }
    p.setPen(QPen(border, hasFocus() && !invalid_ ? 1.4 : 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    if (!isEnabled()) {
        p.fillPath(path, Theme::overlay());
    }

    QTextEdit::paintEvent(event);

    QColor grip = Theme::mutedForeground();
    grip.setAlphaF(0.55);
    p.setPen(QPen(grip, 1.25, Qt::SolidLine, Qt::RoundCap));
    const int x = width() - 6;
    const int y = height() - 6;
    p.drawLine(x - 8, y, x, y - 8);
    p.drawLine(x - 13, y, x, y - 13);
}

}  // namespace edgeqt
