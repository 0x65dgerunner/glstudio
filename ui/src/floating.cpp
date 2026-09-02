#include <edgeqt/button.hpp>
#include <edgeqt/combobox.hpp>
#include <edgeqt/dropdown.hpp>
#include <edgeqt/fonts.hpp>
#include <edgeqt/icons.hpp>
#include <edgeqt/theme.hpp>

#include "internal.hpp"

#include <QEnterEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace edgeqt {
namespace {

class DropdownRow : public QWidget {
public:
    DropdownRow(const QString& text, const QString& shortcut, bool disabled, bool submenu,
                QWidget* parent)
        : QWidget(parent), text_(text), shortcut_(shortcut), disabled_(disabled), submenu_(submenu) {
        setFixedHeight(32);
        setAttribute(Qt::WA_Hover, true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(disabled_ ? Qt::ArrowCursor : Qt::PointingHandCursor);
        setEnabled(!disabled_);
    }

    void setActive(bool active) {
        if (active_ == active) {
            return;
        }
        active_ = active;
        update();
    }

    QSize sizeHint() const override {
        const int textW = QFontMetrics(Fonts::regular(9.5)).horizontalAdvance(text_);
        int extra = 0;
        if (!shortcut_.isEmpty()) {
            extra += QFontMetrics(Fonts::regular(8.5)).horizontalAdvance(shortcut_) + 16;
        }
        if (submenu_) {
            extra += 18;
        }
        return {qMax(200, 20 + textW + extra), 32};
    }

    std::function<void()> onHover;
    std::function<void()> onLeave;
    std::function<void()> onActivate;

protected:
    void enterEvent(QEnterEvent* event) override {
        if (!disabled_ && onHover) {
            onHover();
        }
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        if (onLeave) {
            onLeave();
        }
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (!disabled_ && event->button() == Qt::LeftButton && onActivate) {
            onActivate();
        }
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        Fonts::preparePainter(&painter);
        if (active_ && !disabled_) {
            QPainterPath path;
            path.addRoundedRect(QRectF(rect()), 6, 6);
            painter.fillPath(path, Theme::accent());
        }

        QColor textColor = Theme::foreground();
        if (disabled_) {
            textColor = Theme::mutedForeground();
            textColor.setAlphaF(0.45);
        }

        painter.setPen(textColor);
        painter.setFont(Fonts::regular(9.5));
        const int rightReserve = shortcut_.isEmpty() ? (submenu_ ? 22 : 8) : 8;
        painter.drawText(rect().adjusted(8, 0, -rightReserve, 0), Qt::AlignVCenter | Qt::AlignLeft, text_);

        if (!shortcut_.isEmpty()) {
            painter.setPen(Theme::mutedForeground());
            painter.setFont(Fonts::regular(8.5));
            painter.drawText(rect().adjusted(0, 0, -8, 0), Qt::AlignVCenter | Qt::AlignRight, shortcut_);
        } else if (submenu_) {
            const QPixmap chevron = Icons::pixmap(Icons::stroke(QStringLiteral("chevron-right")), 14,
                                                  Theme::mutedForeground(), devicePixelRatioF());
            painter.drawPixmap(width() - 20, (height() - 14) / 2, chevron);
        }
    }

private:
    QString text_;
    QString shortcut_;
    bool disabled_ = false;
    bool submenu_ = false;
    bool active_ = false;
};

class MenuRule : public QWidget {
public:
    explicit MenuRule(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(9);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(QRect(0, 4, width(), 1), Theme::border());
    }
};

}  // namespace

DropdownMenu::DropdownMenu(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground);
    body_ = new QVBoxLayout(this);
    body_->setContentsMargins(4, 4, 4, 4);
    body_->setSpacing(1);
    setMinimumWidth(180);
}

void DropdownMenu::addLabel(const QString& text) {
    auto* label = new QLabel(text, this);
    label->setFont(Fonts::medium(8.5));
    label->setContentsMargins(8, 6, 8, 4);
    Theme::bindStyle(label, []() {
        return QStringLiteral("color: %1; background: transparent;")
            .arg(Theme::css(Theme::mutedForeground()));
    });
    body_->addWidget(label);
}

void DropdownMenu::addItem(const QString& text, const std::function<void()>& onClick) {
    addItem(text, {}, onClick);
}

void DropdownMenu::addItem(const QString& text, const QString& shortcut,
                           const std::function<void()>& onClick) {
    auto* row = new DropdownRow(text, shortcut, false, false, this);
    row->onHover = [this, row]() {
        hideSubmenu();
        row->setActive(true);
    };
    row->onLeave = [row]() { row->setActive(false); };
    row->onActivate = [this, onClick]() {
        if (onClick) {
            onClick();
        }
        dismiss();
    };
    body_->addWidget(row);
}

void DropdownMenu::addDisabledItem(const QString& text) {
    body_->addWidget(new DropdownRow(text, {}, true, false, this));
}

DropdownMenu* DropdownMenu::addSubmenu(const QString& text) {
    auto* sub = new DropdownMenu(this);
    sub->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    sub->setAttribute(Qt::WA_TranslucentBackground);
    sub->setAttribute(Qt::WA_ShowWithoutActivating);
    sub->setFocusPolicy(Qt::NoFocus);
    sub->setMinimumWidth(128);
    auto* row = new DropdownRow(text, {}, false, true, this);
    row->onHover = [this, sub, row]() {
        cancelHideSubmenu();
        row->setActive(true);
        showSubmenu(sub, row);
    };
    row->onLeave = [this, row]() {
        row->setActive(openSubmenu_ != nullptr && submenuRow_ == row);
        scheduleHideSubmenu();
    };
    row->onActivate = [this, sub, row]() {
        cancelHideSubmenu();
        showSubmenu(sub, row);
    };
    sub->installEventFilter(this);
    body_->addWidget(row);
    return sub;
}

void DropdownMenu::addSeparator() {
    body_->addWidget(new MenuRule(this));
}

void DropdownMenu::popupBelow(QWidget* anchor) {
    adjustSize();
    if (anchor != nullptr) {
        move(anchor->mapToGlobal(QPoint(0, anchor->height() + 4)));
    }
    show();
    raise();
}

void DropdownMenu::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Fonts::preparePainter(&p);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Theme::radiusMd(), Theme::radiusMd());
    p.fillPath(path, Theme::popover());
    p.setPen(QPen(Theme::border(), 1.0));
    p.drawPath(path);
}

void DropdownMenu::hideEvent(QHideEvent* event) {
    hideSubmenu();
    QWidget::hideEvent(event);
}

bool DropdownMenu::eventFilter(QObject* watched, QEvent* event) {
    if (watched == openSubmenu_) {
        if (event->type() == QEvent::Enter) {
            cancelHideSubmenu();
            if (submenuRow_ != nullptr) {
                static_cast<DropdownRow*>(submenuRow_)->setActive(true);
            }
        } else if (event->type() == QEvent::Leave) {
            scheduleHideSubmenu();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DropdownMenu::dismiss() {
    hide();
    if (auto* parent = qobject_cast<DropdownMenu*>(parentWidget())) {
        parent->dismiss();
        return;
    }
    if (testAttribute(Qt::WA_DeleteOnClose)) {
        deleteLater();
    }
}

void DropdownMenu::showSubmenu(DropdownMenu* menu, QWidget* row) {
    if (menu == nullptr || row == nullptr) {
        return;
    }
    if (openSubmenu_ != nullptr && openSubmenu_ != menu) {
        openSubmenu_->hide();
    }
    openSubmenu_ = menu;
    submenuRow_ = row;
    menu->adjustSize();
    menu->move(row->mapToGlobal(QPoint(row->width() + 2, -4)));
    menu->show();
    menu->raise();
}

void DropdownMenu::hideSubmenu() {
    cancelHideSubmenu();
    if (openSubmenu_ != nullptr) {
        openSubmenu_->hide();
        openSubmenu_ = nullptr;
    }
    if (submenuRow_ != nullptr) {
        static_cast<DropdownRow*>(submenuRow_)->setActive(false);
        submenuRow_ = nullptr;
    }
}

void DropdownMenu::cancelHideSubmenu() {
    ++submenuGen_;
}

void DropdownMenu::scheduleHideSubmenu() {
    const int gen = ++submenuGen_;
    QTimer::singleShot(140, this, [this, gen]() {
        if (gen != submenuGen_) {
            return;
        }
        if (openSubmenu_ != nullptr && openSubmenu_->underMouse()) {
            return;
        }
        hideSubmenu();
    });
}

Popover::Popover(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground);
    body_ = new QVBoxLayout(this);
    body_->setContentsMargins(20, 18, 20, 18);
    body_->setSpacing(12);
    setMinimumWidth(288);
}

QVBoxLayout* Popover::bodyLayout() const { return body_; }

void Popover::popupBelow(QWidget* anchor) {
    adjustSize();
    if (anchor != nullptr) {
        move(anchor->mapToGlobal(QPoint(0, anchor->height() + 6)));
    }
    show();
    raise();
}

void Popover::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Fonts::preparePainter(&p);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Theme::radiusLg(), Theme::radiusLg());
    p.fillPath(path, Theme::popover());
    p.setPen(QPen(Theme::border(), 1.0));
    p.drawPath(path);
}

HoverCard::HoverCard(QWidget* trigger) : QObject(trigger), trigger_(trigger) {
    trigger_->installEventFilter(this);
    trigger_->setAttribute(Qt::WA_Hover, true);
}

void HoverCard::setTitle(const QString& title) { title_ = title; }
void HoverCard::setDescription(const QString& description) { description_ = description; }

void HoverCard::showCard() {
    if (card_ == nullptr) {
        auto* pop = new Popover(trigger_);
        card_ = pop;
        auto* title = new QLabel(title_, pop);
        title->setFont(Fonts::medium(10.5));
        auto* desc = new QLabel(description_, pop);
        desc->setWordWrap(true);
        Theme::bindStyle(desc, []() {
            return QStringLiteral("color: %1;").arg(Theme::css(Theme::mutedForeground()));
        });
        pop->bodyLayout()->addWidget(title);
        pop->bodyLayout()->addWidget(desc);
        pop->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    }
    static_cast<Popover*>(card_)->popupBelow(trigger_);
}

void HoverCard::hideCard() {
    if (card_ != nullptr) {
        card_->hide();
    }
}

bool HoverCard::eventFilter(QObject* watched, QEvent* event) {
    if (watched == trigger_) {
        if (event->type() == QEvent::Enter) {
            QTimer::singleShot(220, this, [this]() {
                if (trigger_ != nullptr && trigger_->underMouse()) {
                    showCard();
                }
            });
        } else if (event->type() == QEvent::Leave) {
            QTimer::singleShot(160, this, [this]() { hideCard(); });
        }
    }
    return QObject::eventFilter(watched, event);
}

namespace {

constexpr int kTipArrow = 6;
constexpr int kTipGap = 6;

class TipPopup : public QWidget {
public:
    TipPopup(const QString& text, Tooltip::Side side, QWidget* parent)
        : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
          side_(side) {
        setAttribute(Qt::WA_TranslucentBackground);
        auto* layout = new QVBoxLayout(this);
        int l = 12, t = 8, r = 12, b = 8;
        switch (side_) {
            case Tooltip::Side::Top:
                b += kTipArrow;
                break;
            case Tooltip::Side::Bottom:
                t += kTipArrow;
                break;
            case Tooltip::Side::Left:
                r += kTipArrow;
                break;
            case Tooltip::Side::Right:
                l += kTipArrow;
                break;
        }
        layout->setContentsMargins(l, t, r, b);

        auto* label = new QLabel(text, this);
        label->setFont(Fonts::regular(9.0));
        label->setWordWrap(true);
        label->setMaximumWidth(260);
        Theme::bindStyle(label, []() {
            return QStringLiteral("color: %1; background: transparent;")
                .arg(Theme::css(Theme::background()));
        });
        layout->addWidget(label);
    }

    Tooltip::Side side() const { return side_; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        Fonts::preparePainter(&p);
        const QRectF bounds = QRectF(rect());
        QRectF bubble = bounds.adjusted(0.5, 0.5, -0.5, -0.5);
        switch (side_) {
            case Tooltip::Side::Top:
                bubble.setBottom(bubble.bottom() - kTipArrow);
                break;
            case Tooltip::Side::Bottom:
                bubble.setTop(bubble.top() + kTipArrow);
                break;
            case Tooltip::Side::Left:
                bubble.setRight(bubble.right() - kTipArrow);
                break;
            case Tooltip::Side::Right:
                bubble.setLeft(bubble.left() + kTipArrow);
                break;
        }

        QPainterPath path;
        path.addRoundedRect(bubble, Theme::radiusMd(), Theme::radiusMd());
        const QPointF tip = arrowTip(bubble);
        QPolygonF arrow;
        switch (side_) {
            case Tooltip::Side::Top:
                arrow << QPointF(tip.x() - 5, bubble.bottom()) << QPointF(tip.x() + 5, bubble.bottom())
                      << tip;
                break;
            case Tooltip::Side::Bottom:
                arrow << QPointF(tip.x() - 5, bubble.top()) << QPointF(tip.x() + 5, bubble.top()) << tip;
                break;
            case Tooltip::Side::Left:
                arrow << QPointF(bubble.right(), tip.y() - 5) << QPointF(bubble.right(), tip.y() + 5)
                      << tip;
                break;
            case Tooltip::Side::Right:
                arrow << QPointF(bubble.left(), tip.y() - 5) << QPointF(bubble.left(), tip.y() + 5)
                      << tip;
                break;
        }
        path.addPolygon(arrow);

        p.fillPath(path, Theme::foreground());
        p.setPen(Qt::NoPen);
        p.drawPath(path);
    }

private:
    QPointF arrowTip(const QRectF& bubble) const {
        switch (side_) {
            case Tooltip::Side::Top:
                return {bubble.center().x(), bubble.bottom() + kTipArrow};
            case Tooltip::Side::Bottom:
                return {bubble.center().x(), bubble.top() - kTipArrow};
            case Tooltip::Side::Left:
                return {bubble.right() + kTipArrow, bubble.center().y()};
            case Tooltip::Side::Right:
                return {bubble.left() - kTipArrow, bubble.center().y()};
        }
        return bubble.center();
    }

    Tooltip::Side side_;
};

class TipFilter : public QObject {
public:
    TipFilter(QWidget* target, QString text, Tooltip::Side side)
        : QObject(target), target_(target), text_(std::move(text)), side_(side) {
        target_->installEventFilter(this);
        target_->setAttribute(Qt::WA_Hover, true);
        delay_.setSingleShot(true);
        delay_.setInterval(180);
        connect(&delay_, &QTimer::timeout, this, [this]() { showTip(); });
    }

protected:
    bool eventFilter(QObject*, QEvent* event) override {
        switch (event->type()) {
            case QEvent::Enter:
            case QEvent::FocusIn:
                delay_.start();
                break;
            case QEvent::Leave:
            case QEvent::FocusOut:
                delay_.stop();
                hideTip();
                break;
            default:
                break;
        }
        return false;
    }

private:
    void showTip() {
        if (target_ == nullptr || (!target_->underMouse() && !target_->hasFocus())) {
            return;
        }
        if (tip_ == nullptr) {
            tip_ = new TipPopup(text_, side_, target_);
        }
        tip_->adjustSize();
        tip_->move(tipPos());
        tip_->show();
        tip_->raise();
    }

    void hideTip() {
        if (tip_ != nullptr) {
            tip_->hide();
        }
    }

    QPoint tipPos() const {
        const QSize size = tip_->size();
        const QRect target(target_->mapToGlobal(QPoint(0, 0)), target_->size());
        QPoint pos;
        switch (side_) {
            case Tooltip::Side::Bottom:
                pos = QPoint(target.center().x() - size.width() / 2, target.bottom() + kTipGap);
                break;
            case Tooltip::Side::Top:
                pos = QPoint(target.center().x() - size.width() / 2,
                             target.top() - size.height() - kTipGap);
                break;
            case Tooltip::Side::Left:
                pos = QPoint(target.left() - size.width() - kTipGap,
                             target.center().y() - size.height() / 2);
                break;
            case Tooltip::Side::Right:
                pos = QPoint(target.right() + kTipGap, target.center().y() - size.height() / 2);
                break;
        }
        if (QScreen* screen = target_->screen()) {
            const QRect avail = screen->availableGeometry();
            pos.setX(qBound(avail.left() + 8, pos.x(), avail.right() - size.width() - 8));
            pos.setY(qBound(avail.top() + 8, pos.y(), avail.bottom() - size.height() - 8));
        }
        return pos;
    }

    QWidget* target_ = nullptr;
    QString text_;
    Tooltip::Side side_ = Tooltip::Side::Top;
    QTimer delay_;
    QWidget* tip_ = nullptr;
};

}  // namespace

void Tooltip::install(QWidget* target, const QString& text, Side side) {
    if (target != nullptr) {
        new TipFilter(target, text, side);
    }
}

Combobox::Combobox(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setFont(Fonts::regular(10.0));
    setAttribute(Qt::WA_Hover, true);
}

void Combobox::addItem(const QString& text) { items_.append(text); }
void Combobox::setItems(const QStringList& items) { items_ = items; }
void Combobox::setPlaceholder(const QString& text) { placeholder_ = text; update(); }
QSize Combobox::sizeHint() const { return {220, 36}; }

void Combobox::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        openPopup();
    }
}

void Combobox::enterEvent(QEnterEvent* event) {
    hovered_ = true;
    update();
    QWidget::enterEvent(event);
}

void Combobox::leaveEvent(QEvent* event) {
    hovered_ = false;
    update();
    QWidget::leaveEvent(event);
}

void Combobox::openPopup() {
    auto* popup = new DropdownMenu(this);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    for (const QString& item : items_) {
        popup->addItem(item, [this, item]() {
            current_ = item;
            emit currentTextChanged(item);
            update();
        });
    }
    popup->setMinimumWidth(width());
    popup->popupBelow(this);
}

void Combobox::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Fonts::preparePainter(&p);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Theme::radiusMd(), Theme::radiusMd());
    QColor fill = Theme::input();
    fill.setAlphaF(hovered_ ? 0.32 : 0.22);
    p.fillPath(path, fill);
    p.setPen(QPen(Theme::border(), 1.0));
    p.drawPath(path);
    p.setPen(current_.isEmpty() ? Theme::mutedForeground() : Theme::foreground());
    p.setFont(font());
    p.drawText(rect().adjusted(10, 0, -28, 0), Qt::AlignVCenter | Qt::AlignLeft,
               current_.isEmpty() ? placeholder_ : current_);
    p.drawPixmap(width() - 22, (height() - 14) / 2,
                 Icons::pixmap(Icons::stroke(QStringLiteral("chevron-down")), 14, Theme::mutedForeground(),
                               devicePixelRatioF()));
}

namespace {

class CommandSearch : public QLineEdit {
public:
    explicit CommandSearch(QWidget* parent = nullptr) : QLineEdit(parent) {
        setFont(Fonts::regular(10.5));
        setFrame(false);
        setAttribute(Qt::WA_MacShowFocusRect, false);
        setAutoFillBackground(false);
        setFixedHeight(48);
        setTextMargins(36, 0, 12, 0);
        setPlaceholderText(QStringLiteral("Type a command or search..."));
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

protected:
    void paintEvent(QPaintEvent* event) override {
        QLineEdit::paintEvent(event);
        QPainter painter(this);
        Fonts::preparePainter(&painter);
        const QPixmap icon = Icons::pixmap(Icons::stroke(QStringLiteral("search")), 16,
                                           Theme::mutedForeground(), devicePixelRatioF());
        painter.drawPixmap(16, (height() - 16) / 2, icon);
    }
};

class CommandRow : public QWidget {
public:
    CommandRow(const QString& text, const QString& icon, const QString& shortcut, QWidget* parent)
        : QWidget(parent), text_(text), icon_(icon), shortcut_(shortcut) {
        setFixedHeight(36);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setActive(bool active) {
        if (active_ == active) {
            return;
        }
        active_ = active;
        update();
    }

    std::function<void()> onHover;
    std::function<void()> onActivate;

protected:
    void enterEvent(QEnterEvent* event) override {
        if (onHover) {
            onHover();
        }
        QWidget::enterEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && onActivate) {
            onActivate();
        }
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        Fonts::preparePainter(&painter);
        const QRectF bounds = QRectF(rect());
        if (active_) {
            QPainterPath path;
            path.addRoundedRect(bounds, 6, 6);
            painter.fillPath(path, Theme::muted());
        }

        const QColor iconColor = active_ ? Theme::foreground() : Theme::mutedForeground();
        if (!icon_.isEmpty()) {
            const QPixmap pix =
                Icons::pixmap(Icons::stroke(icon_), 16, iconColor, devicePixelRatioF());
            painter.drawPixmap(8, (height() - 16) / 2, pix);
        }

        painter.setPen(Theme::foreground());
        painter.setFont(Fonts::regular(10.0));
        const int textLeft = icon_.isEmpty() ? 10 : 32;
        const int shortcutWidth =
            shortcut_.isEmpty() ? 0 : QFontMetrics(Fonts::regular(9.0)).horizontalAdvance(shortcut_) + 12;
        painter.drawText(QRect(textLeft, 0, width() - textLeft - shortcutWidth - 8, height()),
                         Qt::AlignVCenter | Qt::AlignLeft, text_);

        if (!shortcut_.isEmpty()) {
            painter.setPen(Theme::mutedForeground());
            painter.setFont(Fonts::regular(9.0));
            painter.drawText(rect().adjusted(0, 0, -10, 0), Qt::AlignVCenter | Qt::AlignRight, shortcut_);
        }
    }

private:
    QString text_;
    QString icon_;
    QString shortcut_;
    bool active_ = false;
};

}  // namespace

Command::Command(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setMinimumWidth(384);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    search_ = new CommandSearch(this);
    search_->installEventFilter(this);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { rebuild(); });
    root->addWidget(search_);

    auto* line = new QWidget(this);
    line->setFixedHeight(1);
    Theme::bindStyle(line, []() {
        return QStringLiteral("background: %1;").arg(Theme::css(Theme::border()));
    });
    root->addWidget(line);

    auto* list = new QWidget(this);
    list_ = list;
    listLayout_ = new QVBoxLayout(list_);
    listLayout_->setContentsMargins(6, 6, 6, 8);
    listLayout_->setSpacing(2);
    list_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(list_);

    connect(&Theme::instance(), &Theme::changed, this, [this]() { update(); });
}

void Command::addItem(const QString& text, const std::function<void()>& onPick) {
    addItem({}, text, {}, {}, onPick);
}

void Command::addItem(const QString& group, const QString& text, const QString& icon,
                      const QString& shortcut, const std::function<void()>& onPick) {
    items_.push_back({group, text, icon, shortcut, onPick});
    rebuild();
}

void Command::setPlaceholder(const QString& text) {
    search_->setPlaceholderText(text);
}

void Command::focusInput() {
    search_->setFocus(Qt::OtherFocusReason);
}

void Command::reset() {
    selected_ = 0;
    if (search_->text().isEmpty()) {
        rebuild();
        return;
    }
    search_->clear();
}

QSize Command::sizeHint() const {
    const int listH = list_ != nullptr ? list_->minimumSizeHint().height() : 0;
    const int searchH = search_ != nullptr ? search_->sizeHint().height() : 48;
    return {384, searchH + 1 + listH};
}

QSize Command::minimumSizeHint() const {
    return sizeHint();
}

void Command::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    Fonts::preparePainter(&painter);
    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(bounds, Theme::radiusXl(), Theme::radiusXl());
    painter.fillPath(path, Theme::popover());
    painter.setPen(QPen(Theme::border(), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

bool Command::eventFilter(QObject* watched, QEvent* event) {
    if (watched == search_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Down) {
            moveSelection(1);
            return true;
        }
        if (key->key() == Qt::Key_Up) {
            moveSelection(-1);
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            activateSelection();
            return true;
        }
        if (key->key() == Qt::Key_Escape) {
            emit dismissed();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void Command::rebuild() {
    while (QLayoutItem* item = listLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    rows_.clear();

    const QString query = search_->text().trimmed();
    QString lastGroup;
    int visible = 0;
    for (int i = 0; i < items_.size(); ++i) {
        const Item& item = items_.at(i);
        if (!query.isEmpty() && !item.text.contains(query, Qt::CaseInsensitive) &&
            !item.group.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        if (!item.group.isEmpty() && item.group != lastGroup) {
            lastGroup = item.group;
            auto* heading = new QLabel(item.group, list_);
            heading->setFont(Fonts::medium(8.5));
            heading->setFixedHeight(28);
            heading->setContentsMargins(8, 6, 8, 4);
            heading->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            Theme::bindStyle(heading, []() {
                return QStringLiteral("color: %1; background: transparent;")
                    .arg(Theme::css(Theme::mutedForeground()));
            });
            listLayout_->addWidget(heading);
        }

        auto* row = new CommandRow(item.text, item.icon, item.shortcut, list_);
        const int rowIndex = rows_.size();
        row->onHover = [this, rowIndex]() { setSelected(rowIndex); };
        row->onActivate = [this, item]() {
            if (item.onPick) {
                item.onPick();
            }
            emit itemPicked(item.text);
        };
        listLayout_->addWidget(row);
        rows_.push_back(row);
        ++visible;
    }

    if (visible == 0) {
        auto* empty = new QLabel(QStringLiteral("No results found."), list_);
        empty->setAlignment(Qt::AlignCenter);
        empty->setFont(Fonts::regular(10.0));
        empty->setMinimumHeight(64);
        Theme::bindStyle(empty, []() {
            return QStringLiteral("color: %1; background: transparent;")
                .arg(Theme::css(Theme::mutedForeground()));
        });
        listLayout_->addWidget(empty);
        selected_ = -1;
    } else {
        setSelected(qBound(0, selected_, rows_.size() - 1));
    }

    relayout();
    emit contentsChanged();
}

void Command::relayout() {
    if (list_ == nullptr || listLayout_ == nullptr || search_ == nullptr) {
        return;
    }
    listLayout_->activate();
    const int listH = listLayout_->sizeHint().height();
    list_->setFixedHeight(listH);
    setFixedSize(384, search_->sizeHint().height() + 1 + listH);
    updateGeometry();
}

void Command::setSelected(int index) {
    selected_ = index;
    for (int i = 0; i < rows_.size(); ++i) {
        static_cast<CommandRow*>(rows_.at(i))->setActive(i == selected_);
    }
}

void Command::moveSelection(int delta) {
    if (rows_.isEmpty()) {
        return;
    }
    int next = selected_ + delta;
    if (next < 0) {
        next = rows_.size() - 1;
    } else if (next >= rows_.size()) {
        next = 0;
    }
    setSelected(next);
}

void Command::activateSelection() {
    if (selected_ < 0 || selected_ >= rows_.size()) {
        return;
    }
    if (auto* row = static_cast<CommandRow*>(rows_.at(selected_))) {
        if (row->onActivate) {
            row->onActivate();
        }
    }
}

CommandPalette::CommandPalette(QWidget* context) : QWidget(internal::overlayHost(context)) {
    hide();
    setFocusPolicy(Qt::StrongFocus);

    auto* scrim = new internal::Scrim(this);
    scrim_ = scrim;
    connect(scrim, &internal::Scrim::clicked, this, &CommandPalette::closePopup);

    command_ = new Command(this);
    connect(command_, &Command::itemPicked, this, &CommandPalette::closePopup);
    connect(command_, &Command::dismissed, this, &CommandPalette::closePopup);
    connect(command_, &Command::contentsChanged, this, [this]() {
        if (open_) {
            QTimer::singleShot(0, this, [this]() {
                if (open_) {
                    layoutOverlay();
                }
            });
        }
    });

    if (parentWidget() != nullptr) {
        parentWidget()->installEventFilter(this);
    }
}

void CommandPalette::addItem(const QString& text, const std::function<void()>& onPick) {
    command_->addItem(text, onPick);
}

void CommandPalette::addItem(const QString& group, const QString& text, const QString& icon,
                             const QString& shortcut, const std::function<void()>& onPick) {
    command_->addItem(group, text, icon, shortcut, onPick);
}

void CommandPalette::setPlaceholder(const QString& text) {
    command_->setPlaceholder(text);
}

void CommandPalette::layoutOverlay() {
    if (parentWidget() == nullptr || command_ == nullptr) {
        return;
    }
    setGeometry(parentWidget()->rect());
    scrim_->setGeometry(rect());
    command_->adjustSize();
    const int x = (width() - command_->width()) / 2;
    const int y = qMax(48, height() / 5);
    command_->move(x, y);
}

void CommandPalette::open() {
    if (open_ || parentWidget() == nullptr) {
        return;
    }
    open_ = true;
    command_->reset();
    show();
    raise();
    layoutOverlay();
    static_cast<internal::Scrim*>(scrim_)->setOpacity(1.0);
    if (auto* host = qobject_cast<internal::OverlayHost*>(parentWidget())) {
        host->refreshPassthrough();
    }
    command_->focusInput();
}

void CommandPalette::closePopup() {
    if (!open_) {
        return;
    }
    open_ = false;
    hide();
    if (auto* host = qobject_cast<internal::OverlayHost*>(parentWidget())) {
        host->refreshPassthrough();
    }
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize && open_) {
        layoutOverlay();
    }
    return QWidget::eventFilter(watched, event);
}

void CommandPalette::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        closePopup();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CommandPalette::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (open_) {
        layoutOverlay();
    } else if (scrim_ != nullptr) {
        scrim_->setGeometry(rect());
    }
}

}  // namespace edgeqt
