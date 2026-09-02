#include <edgeqt/alert.hpp>
#include <edgeqt/avatar.hpp>
#include <edgeqt/badge.hpp>
#include <edgeqt/breadcrumb.hpp>
#include <edgeqt/calendar.hpp>
#include <edgeqt/empty.hpp>
#include <edgeqt/fonts.hpp>
#include <edgeqt/icons.hpp>
#include <edgeqt/theme.hpp>

#include "internal.hpp"

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

#include <cmath>

namespace edgeqt {
namespace {

void paintRounded(QPainter* p, const QRect& rect, int radius, const QColor& fill, const QColor& border) {
    Fonts::preparePainter(p);
    const QRectF bounds = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(bounds, radius, radius);
    p->fillPath(path, fill);
    p->setPen(QPen(border, 1.0));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path);
}

class EmptyMedia : public QWidget {
public:
    explicit EmptyMedia(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(40, 40);
        setAttribute(Qt::WA_StyledBackground, false);
        setAutoFillBackground(false);
    }

    void setIconName(const QString& name) {
        iconName_ = name;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        Fonts::preparePainter(&painter);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), 10, 10);
        painter.fillPath(path, Theme::muted());
        if (!iconName_.isEmpty()) {
            const QPixmap pix =
                Icons::pixmap(Icons::stroke(iconName_), 20, Theme::foreground(), devicePixelRatioF());
            painter.drawPixmap((width() - 20) / 2, (height() - 20) / 2, pix);
        }
    }

private:
    QString iconName_ = QStringLiteral("folder-code");
};

}  // namespace

Alert::Alert(QWidget* parent) : QFrame(parent) {
    setAttribute(Qt::WA_StyledBackground, false);
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(12);
    icon_ = new QLabel(this);
    icon_->setFixedSize(16, 16);
    root->addWidget(icon_, 0, Qt::AlignTop);
    auto* col = new QWidget(this);
    auto* colLayout = new QVBoxLayout(col);
    colLayout->setContentsMargins(0, 0, 0, 0);
    colLayout->setSpacing(4);
    title_ = new QLabel(col);
    title_->setFont(Fonts::medium(10.5));
    colLayout->addWidget(title_);
    description_ = new QLabel(col);
    description_->setWordWrap(true);
    description_->setFont(Fonts::regular(9.5));
    Theme::bindStyle(description_, []() {
        return QStringLiteral("color: %1;").arg(Theme::css(Theme::mutedForeground()));
    });
    colLayout->addWidget(description_);
    extra_ = new QVBoxLayout();
    colLayout->addLayout(extra_);
    root->addWidget(col, 1);
    refreshIcon();
    connect(&Theme::instance(), &Theme::changed, this, [this]() { refreshIcon(); });
}

void Alert::setVariant(Variant variant) {
    variant_ = variant;
    refreshIcon();
    update();
}

void Alert::refreshIcon() {
    const QString name = variant_ == Variant::Destructive ? QStringLiteral("error") : QStringLiteral("info");
    const QColor color = variant_ == Variant::Destructive ? Theme::destructive() : Theme::foreground();
    icon_->setPixmap(Icons::pixmap(Icons::stroke(name), 16, color, devicePixelRatioF()));
}

void Alert::setTitle(const QString& title) { title_->setText(title); }
void Alert::setDescription(const QString& description) { description_->setText(description); }
QVBoxLayout* Alert::extraLayout() const { return extra_; }

void Alert::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QColor border = variant_ == Variant::Destructive ? Theme::destructive() : Theme::border();
    QColor fill = Theme::card();
    if (variant_ == Variant::Destructive) {
        fill = Theme::destructive();
        fill.setAlphaF(0.08);
    }
    paintRounded(&p, rect(), Theme::radiusLg(), fill, border);
}

Avatar::Avatar(QWidget* parent) : QWidget(parent) {
    setFixedSize(size_, size_);
}

void Avatar::setSize(int pixelSize) {
    size_ = pixelSize;
    setFixedSize(size_, size_);
    update();
}

void Avatar::setInitials(const QString& initials) {
    initials_ = initials;
    update();
}

void Avatar::setImage(const QPixmap& pixmap) {
    image_ = pixmap;
    update();
}

QSize Avatar::sizeHint() const { return {size_, size_}; }

void Avatar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Fonts::preparePainter(&p);
    QPainterPath clip;
    clip.addEllipse(QRectF(rect()).adjusted(1, 1, -1, -1));
    p.setClipPath(clip);
    if (!image_.isNull()) {
        p.drawPixmap(rect(), image_);
    } else {
        p.fillPath(clip, Theme::secondary());
        p.setPen(Theme::foreground());
        p.setFont(Fonts::medium(size_ * 0.32));
        p.drawText(rect(), Qt::AlignCenter, initials_);
    }
}

Badge::Badge(const QString& text, QWidget* parent) : QWidget(parent), text_(text) {
    setFont(Fonts::medium(8.5));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void Badge::setText(const QString& text) { text_ = text; updateGeometry(); update(); }
void Badge::setVariant(Variant variant) { variant_ = variant; update(); }

QSize Badge::sizeHint() const {
    return {QFontMetrics(font()).horizontalAdvance(text_) + 16, 22};
}

void Badge::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QColor fill = Theme::primary();
    QColor fg = Theme::primaryForeground();
    QColor border = Qt::transparent;
    switch (variant_) {
        case Variant::Secondary:
            fill = Theme::secondary();
            fg = Theme::foreground();
            break;
        case Variant::Outline:
            fill = Qt::transparent;
            fg = Theme::foreground();
            border = Theme::border();
            break;
        case Variant::Destructive:
            fill = Theme::destructive();
            fill.setAlphaF(0.16);
            fg = Theme::destructive();
            break;
        case Variant::Default:
        default:
            break;
    }
    paintRounded(&p, rect(), 999, fill, border);
    p.setPen(fg);
    p.setFont(font());
    p.drawText(rect(), Qt::AlignCenter, text_);
}

Breadcrumb::Breadcrumb(QWidget* parent) : QWidget(parent) {
    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(0, 0, 0, 0);
    row_->setSpacing(6);
    row_->addStretch(1);
}

void Breadcrumb::clear() {
    while (QLayoutItem* item = row_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    row_->addStretch(1);
    count_ = 0;
}

void Breadcrumb::addItem(const QString& text, const std::function<void()>& onClick) {
    if (count_ > 0) {
        auto* chevron = new QLabel(this);
        chevron->setPixmap(Icons::pixmap(Icons::stroke(QStringLiteral("chevron-right")), 12,
                                         Theme::mutedForeground(), devicePixelRatioF()));
        row_->insertWidget(row_->count() - 1, chevron);
    }
    auto* item = new QPushButton(text, this);
    item->setCursor(onClick ? Qt::PointingHandCursor : Qt::ArrowCursor);
    item->setFlat(true);
    item->setFont(Fonts::regular(9.5));
    item->setStyleSheet(QString());
    Theme::bindStyle(item, []() {
        return QStringLiteral(
                   "QPushButton { border: none; background: transparent; color: %1; padding: 0; }"
                   "QPushButton:hover { color: %2; }")
            .arg(Theme::css(Theme::mutedForeground()), Theme::css(Theme::foreground()));
    });
    if (onClick) {
        connect(item, &QPushButton::clicked, this, [onClick]() { onClick(); });
    }
    row_->insertWidget(row_->count() - 1, item);
    ++count_;
}

Calendar::Calendar(QWidget* parent) : QWidget(parent) {
    setMinimumSize(280, 280);
    setFont(Fonts::regular(9.5));
}

void Calendar::setSelectedDate(const QDate& date) {
    selected_ = date;
    visible_ = date;
    update();
}

QRect Calendar::cellRect(int row, int col) const {
    const int top = 72;
    const int gridH = height() - top - 12;
    const int w = (width() - 24) / 7;
    const int h = gridH / 6;
    return {12 + col * w, top + row * h, w, h};
}

void Calendar::shiftMonth(int delta) {
    visible_ = visible_.addMonths(delta);
    update();
}

void Calendar::mousePressEvent(QMouseEvent* event) {
    if (event->pos().y() < 40) {
        if (event->pos().x() < width() / 2) {
            shiftMonth(-1);
        } else {
            shiftMonth(1);
        }
        return;
    }
    for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 7; ++c) {
            if (cellRect(r, c).contains(event->pos())) {
                const QDate first(visible_.year(), visible_.month(), 1);
                const int start = first.dayOfWeek() % 7;
                const QDate date = first.addDays(r * 7 + c - start);
                selected_ = date;
                emit dateSelected(date);
                update();
                return;
            }
        }
    }
}

void Calendar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    paintRounded(&p, rect(), Theme::radiusXl(), Theme::card(), Theme::border());
    p.setPen(Theme::foreground());
    p.setFont(Fonts::medium(10.5));
    p.drawText(QRect(36, 12, width() - 72, 28), Qt::AlignCenter, visible_.toString(QStringLiteral("MMMM yyyy")));
    p.drawPixmap(16, 18, Icons::pixmap(Icons::stroke(QStringLiteral("chevron-left")), 14,
                                       Theme::mutedForeground(), devicePixelRatioF()));
    p.drawPixmap(width() - 30, 18, Icons::pixmap(Icons::stroke(QStringLiteral("chevron-right")), 14,
                                                 Theme::mutedForeground(), devicePixelRatioF()));
    p.setFont(Fonts::regular(9.0));
    p.setPen(Theme::mutedForeground());
    const char* days[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    for (int i = 0; i < 7; ++i) {
        p.drawText(QRect(12 + i * ((width() - 24) / 7), 44, (width() - 24) / 7, 20), Qt::AlignCenter,
                   QString::fromLatin1(days[i]));
    }
    const QDate first(visible_.year(), visible_.month(), 1);
    const int start = first.dayOfWeek() % 7;
    for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 7; ++c) {
            const QDate date = first.addDays(r * 7 + c - start);
            const QRect cell = cellRect(r, c);
            if (date == selected_) {
                QPainterPath path;
                path.addEllipse(QRectF(cell).adjusted(6, 4, -6, -4));
                p.fillPath(path, Theme::primary());
                p.setPen(Theme::primaryForeground());
            } else if (date == QDate::currentDate()) {
                p.setPen(Theme::foreground());
            } else if (date.month() != visible_.month()) {
                p.setPen(Theme::mutedForeground());
            } else {
                p.setPen(Theme::foreground());
            }
            p.setFont(Fonts::regular(9.0));
            p.drawText(cell, Qt::AlignCenter, QString::number(date.day()));
        }
    }
}

Empty::Empty(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    auto* root = new QVBoxLayout(this);
    root->setAlignment(Qt::AlignCenter);
    root->setContentsMargins(24, 16, 24, 16);
    root->setSpacing(0);

    media_ = new EmptyMedia(this);
    static_cast<EmptyMedia*>(media_)->setIconName(iconName_);
    root->addWidget(media_, 0, Qt::AlignCenter);
    root->addSpacing(16);

    auto* title = new QLabel(this);
    titleLabel_ = title;
    title->setAlignment(Qt::AlignCenter);
    title->setFont(Fonts::medium(13.5));
    root->addWidget(title);
    root->addSpacing(6);

    auto* desc = new QLabel(this);
    descriptionLabel_ = desc;
    desc->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    desc->setWordWrap(true);
    desc->setMaximumWidth(280);
    desc->setFont(Fonts::regular(10.0));
    Theme::bindStyle(desc, []() {
        return QStringLiteral("color: %1; background: transparent;")
            .arg(Theme::css(Theme::mutedForeground()));
    });
    root->addWidget(desc, 0, Qt::AlignCenter);
    root->addSpacing(20);

    auto* actionsWrap = new QWidget(this);
    actions_ = new QHBoxLayout(actionsWrap);
    actions_->setContentsMargins(0, 0, 0, 0);
    actions_->setSpacing(8);
    actions_->setAlignment(Qt::AlignCenter);
    root->addWidget(actionsWrap, 0, Qt::AlignCenter);
    root->addSpacing(16);

    auto* footerWrap = new QWidget(this);
    footer_ = new QVBoxLayout(footerWrap);
    footer_->setContentsMargins(0, 0, 0, 0);
    footer_->setSpacing(0);
    footer_->setAlignment(Qt::AlignCenter);
    root->addWidget(footerWrap, 0, Qt::AlignCenter);

    refreshIcon();
    connect(&Theme::instance(), &Theme::changed, this, [this]() { refreshIcon(); });
}

void Empty::setIcon(const QString& strokeName) {
    iconName_ = strokeName;
    refreshIcon();
}

void Empty::refreshIcon() {
    if (media_ != nullptr) {
        static_cast<EmptyMedia*>(media_)->setIconName(iconName_);
    }
}

void Empty::setTitle(const QString& title) { static_cast<QLabel*>(titleLabel_)->setText(title); }
void Empty::setDescription(const QString& description) {
    static_cast<QLabel*>(descriptionLabel_)->setText(description);
}
QHBoxLayout* Empty::actionLayout() const { return actions_; }
QVBoxLayout* Empty::footerLayout() const { return footer_; }

Kbd::Kbd(const QString& text, QWidget* parent) : QWidget(parent), text_(text) {
    setFont(Fonts::medium(8.5));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}
void Kbd::setText(const QString& text) { text_ = text; updateGeometry(); update(); }
QSize Kbd::sizeHint() const { return {QFontMetrics(font()).horizontalAdvance(text_) + 14, 22}; }
void Kbd::paintEvent(QPaintEvent*) {
    QPainter p(this);
    paintRounded(&p, rect(), 6, Theme::secondary(), Theme::border());
    p.setPen(Theme::foreground());
    p.setFont(font());
    p.drawText(rect(), Qt::AlignCenter, text_);
}

Label::Label(const QString& text, QWidget* parent) : QWidget(parent), text_(text) {
    setFont(Fonts::medium(9.5));
}
void Label::setText(const QString& text) { text_ = text; updateGeometry(); update(); }
void Label::setMuted(bool muted) { muted_ = muted; update(); }
QSize Label::sizeHint() const { return {QFontMetrics(font()).horizontalAdvance(text_), 18}; }
void Label::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Fonts::preparePainter(&p);
    p.setPen(muted_ ? Theme::mutedForeground() : Theme::foreground());
    p.setFont(font());
    p.drawText(rect(), Qt::AlignVCenter | Qt::AlignLeft, text_);
}

Separator::Separator(Qt::Orientation orientation, QWidget* parent) : QWidget(parent) {
    if (orientation == Qt::Horizontal) {
        setFixedHeight(1);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    } else {
        setFixedWidth(1);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }
}
void Separator::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Theme::border());
}

Skeleton::Skeleton(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(16);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    anim_ = new QPropertyAnimation(this, "phase", this);
    anim_->setDuration(1800);
    anim_->setStartValue(0.0);
    anim_->setEndValue(1.0);
    anim_->setLoopCount(-1);
    anim_->setEasingCurve(QEasingCurve::Linear);
}

void Skeleton::setPhase(qreal value) {
    phase_ = value - std::floor(value);
    update();
}

void Skeleton::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (anim_ != nullptr && anim_->state() != QAbstractAnimation::Running) {
        anim_->start();
    }
}

void Skeleton::hideEvent(QHideEvent* event) {
    if (anim_ != nullptr) {
        anim_->pause();
    }
    QWidget::hideEvent(event);
}

void Skeleton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Fonts::preparePainter(&p);

    const qreal tau = 6.283185307179586;
    const qreal wave = 0.5 - 0.5 * std::cos(phase_ * tau);

    QColor fill = Theme::muted();
    fill.setAlphaF(0.40 + 0.16 * wave);

    const qreal radius = qMin<qreal>(Theme::radiusMd(), qMin(width(), height()) * 0.5);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()), radius, radius);
    p.fillPath(path, fill);

    p.setClipPath(path);
    const qreal shineW = qMax(56.0, width() * 0.55);
    const qreal travel = width() + shineW * 2.0;
    const qreal x = -shineW + travel * phase_;
    QLinearGradient shine(x, 0.0, x + shineW, 0.0);
    QColor highlight = Theme::foreground();
    highlight.setAlphaF(0.05 + 0.07 * wave);
    shine.setColorAt(0.0, Qt::transparent);
    shine.setColorAt(0.5, highlight);
    shine.setColorAt(1.0, Qt::transparent);
    p.fillRect(rect(), shine);
}

Progress::Progress(QWidget* parent) : QWidget(parent) {
    setFixedHeight(10);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}
void Progress::setValue(int value) { value_ = qBound(0, value, 100); update(); }
QSize Progress::sizeHint() const { return {280, 10}; }
void Progress::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Fonts::preparePainter(&p);
    const QRectF bounds = QRectF(rect());
    const qreal radius = bounds.height() * 0.5;
    QPainterPath track;
    track.addRoundedRect(bounds, radius, radius);
    p.fillPath(track, Theme::muted());

    const qreal fillWidth = bounds.width() * (value_ / 100.0);
    if (fillWidth > 0.5) {
        p.setClipPath(track);
        QPainterPath fill;
        fill.addRoundedRect(QRectF(bounds.left(), bounds.top(), fillWidth, bounds.height()), radius,
                            radius);
        p.fillPath(fill, Theme::primary());
    }
}

}  // namespace edgeqt
