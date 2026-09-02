#include "internal.hpp"

#include <edgeqt/theme.hpp>
#include <edgeqt/window.hpp>

#include <QChildEvent>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVBoxLayout>

namespace edgeqt::internal {
namespace {

void configureAnimation(QPropertyAnimation* animation, int duration, const QEasingCurve& curve) {
    animation->setDuration(duration);
    animation->setEasingCurve(curve);
}

}  // namespace

QEasingCurve easeOut() {
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(0.16, 1.0), QPointF(0.3, 1.0), QPointF(1.0, 1.0));
    return curve;
}

QEasingCurve easeEmphasized() {
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(0.22, 1.0), QPointF(0.36, 1.0), QPointF(1.0, 1.0));
    return curve;
}

QEasingCurve easeSheet() {
    return QEasingCurve(QEasingCurve::InOutCubic);
}

QWidget* chromeHost(QWidget* from) {
    if (from == nullptr) {
        return nullptr;
    }
    if (auto* window = qobject_cast<Window*>(from->window())) {
        return window->shell();
    }
    return from->window();
}

QWidget* overlayHost(QWidget* from) {
    QWidget* host = chromeHost(from);
    if (host == nullptr) {
        return nullptr;
    }
    if (auto* window = qobject_cast<Window*>(from->window())) {
        return window->overlayHost();
    }
    auto* overlay = host->findChild<OverlayHost*>(QStringLiteral("ShOverlayHost"), Qt::FindDirectChildrenOnly);
    if (overlay == nullptr) {
        overlay = new OverlayHost(host);
        overlay->setObjectName(QStringLiteral("ShOverlayHost"));
        overlay->setGeometry(host->rect());
        overlay->raise();
        overlay->show();
        host->installEventFilter(overlay);
    }
    return overlay;
}

QWidget* toastHost(QWidget* from) {
    if (from == nullptr) {
        return nullptr;
    }
    if (auto* window = qobject_cast<Window*>(from->window())) {
        return window->toastHost();
    }
    QWidget* host = chromeHost(from);
    auto* viewport = host->findChild<ToastViewport*>(QStringLiteral("ShToastViewport"), Qt::FindDirectChildrenOnly);
    if (viewport == nullptr) {
        viewport = new ToastViewport(host);
        viewport->setObjectName(QStringLiteral("ShToastViewport"));
        viewport->raise();
        viewport->show();
        host->installEventFilter(viewport);
        viewport->relayout();
    }
    return viewport;
}

void fadeTo(QWidget* widget, qreal target, int duration, const QEasingCurve& curve,
            const std::function<void()>& done) {
    if (widget == nullptr) {
        return;
    }

    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(widget->graphicsEffect());
    if (effect == nullptr) {
        effect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(effect);
    }

    auto* animation = new QPropertyAnimation(effect, "opacity", widget);
    configureAnimation(animation, duration, curve);
    animation->setStartValue(effect->opacity());
    animation->setEndValue(target);
    QObject::connect(animation, &QPropertyAnimation::finished, widget, [animation, done]() {
        animation->deleteLater();
        if (done) {
            done();
        }
    });
    animation->start();
}

void moveTo(QWidget* widget, const QRect& target, int duration, const QEasingCurve& curve,
            const std::function<void()>& done) {
    if (widget == nullptr) {
        return;
    }

    auto* animation = new QPropertyAnimation(widget, "geometry", widget);
    configureAnimation(animation, duration, curve);
    animation->setStartValue(widget->geometry());
    animation->setEndValue(target);
    QObject::connect(animation, &QPropertyAnimation::finished, widget, [animation, done]() {
        animation->deleteLater();
        if (done) {
            done();
        }
    });
    animation->start();
}

void playOverlay(Scrim* scrim, QWidget* panel, const QRect& from, const QRect& to, bool opening,
                 int duration, const QEasingCurve& curve, const std::function<void()>& done) {
    if (scrim == nullptr || panel == nullptr) {
        return;
    }

    panel->setGeometry(from);
    auto* group = new QParallelAnimationGroup(panel);

    auto* scrimAnim = new QPropertyAnimation(scrim, "opacity", group);
    configureAnimation(scrimAnim, duration, curve);
    scrimAnim->setStartValue(opening ? 0.0 : scrim->opacity());
    scrimAnim->setEndValue(opening ? 1.0 : 0.0);
    group->addAnimation(scrimAnim);

    auto* panelAnim = new QPropertyAnimation(panel, "geometry", group);
    configureAnimation(panelAnim, duration, curve);
    panelAnim->setStartValue(from);
    panelAnim->setEndValue(to);
    group->addAnimation(panelAnim);

    auto* fade = qobject_cast<QGraphicsOpacityEffect*>(panel->graphicsEffect());
    if (fade == nullptr) {
        fade = new QGraphicsOpacityEffect(panel);
        panel->setGraphicsEffect(fade);
    }
    auto* fadeAnim = new QPropertyAnimation(fade, "opacity", group);
    configureAnimation(fadeAnim, duration, curve);
    fadeAnim->setStartValue(opening ? 0.0 : fade->opacity());
    fadeAnim->setEndValue(opening ? 1.0 : 0.0);
    group->addAnimation(fadeAnim);

    QObject::connect(group, &QParallelAnimationGroup::finished, panel, [group, done]() {
        group->deleteLater();
        if (done) {
            done();
        }
    });
    group->start();
}

Scrim::Scrim(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
}

void Scrim::setOpacity(qreal opacity) {
    opacity_ = qBound(0.0, opacity, 1.0);
    update();
}

void Scrim::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor color = Theme::overlay();
    color.setAlphaF(color.alphaF() * opacity_);
    painter.fillRect(rect(), color);
}

void Scrim::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

Panel::Panel(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
}

void Panel::setCorners(Corners corners) {
    corners_ = corners;
    update();
}

void Panel::setRadius(int radius) {
    radius_ = radius;
    update();
}

void Panel::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    const qreal r = radius_;

    switch (corners_) {
        case Corners::None:
            path.addRect(bounds);
            break;
        case Corners::Top:
            path.moveTo(bounds.left(), bounds.bottom());
            path.lineTo(bounds.left(), bounds.top() + r);
            path.quadTo(bounds.left(), bounds.top(), bounds.left() + r, bounds.top());
            path.lineTo(bounds.right() - r, bounds.top());
            path.quadTo(bounds.right(), bounds.top(), bounds.right(), bounds.top() + r);
            path.lineTo(bounds.right(), bounds.bottom());
            path.closeSubpath();
            break;
        case Corners::Bottom:
            path.moveTo(bounds.left(), bounds.top());
            path.lineTo(bounds.right(), bounds.top());
            path.lineTo(bounds.right(), bounds.bottom() - r);
            path.quadTo(bounds.right(), bounds.bottom(), bounds.right() - r, bounds.bottom());
            path.lineTo(bounds.left() + r, bounds.bottom());
            path.quadTo(bounds.left(), bounds.bottom(), bounds.left(), bounds.bottom() - r);
            path.closeSubpath();
            break;
        case Corners::Left:
            path.moveTo(bounds.right(), bounds.top());
            path.lineTo(bounds.right(), bounds.bottom());
            path.lineTo(bounds.left() + r, bounds.bottom());
            path.quadTo(bounds.left(), bounds.bottom(), bounds.left(), bounds.bottom() - r);
            path.lineTo(bounds.left(), bounds.top() + r);
            path.quadTo(bounds.left(), bounds.top(), bounds.left() + r, bounds.top());
            path.closeSubpath();
            break;
        case Corners::Right:
            path.moveTo(bounds.left(), bounds.top());
            path.lineTo(bounds.left(), bounds.bottom());
            path.lineTo(bounds.right() - r, bounds.bottom());
            path.quadTo(bounds.right(), bounds.bottom(), bounds.right(), bounds.bottom() - r);
            path.lineTo(bounds.right(), bounds.top() + r);
            path.quadTo(bounds.right(), bounds.top(), bounds.right() - r, bounds.top());
            path.closeSubpath();
            break;
        case Corners::All:
        default:
            path.addRoundedRect(bounds, r, r);
            break;
    }

    painter.fillPath(path, Theme::popover());
    painter.setPen(QPen(Theme::border(), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

OverlayHost::OverlayHost(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    hide();
}

void OverlayHost::refreshPassthrough() {
    bool blocking = false;
    const auto kids = findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* child : kids) {
        if (!child->isHidden()) {
            blocking = true;
            break;
        }
    }
    setAttribute(Qt::WA_TransparentForMouseEvents, !blocking);
    if (blocking) {
        show();
        raise();
    } else {
        hide();
    }
}

void OverlayHost::childEvent(QChildEvent* event) {
    QWidget::childEvent(event);
    refreshPassthrough();
}

bool OverlayHost::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        if (auto* parent = parentWidget()) {
            setGeometry(parent->rect());
        }
    }
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
        refreshPassthrough();
    }
    return QWidget::eventFilter(watched, event);
}

ToastViewport::ToastViewport(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);
    if (parent != nullptr) {
        parent->installEventFilter(this);
    }
}

int ToastViewport::heightHint() const {
    const auto kids = findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    QList<QWidget*> cards;
    for (QWidget* child : kids) {
        if (!child->isHidden()) {
            cards.append(child);
        }
    }
    if (cards.isEmpty()) {
        return 8;
    }
    if (expanded_) {
        int stack = 16;
        for (QWidget* card : cards) {
            stack += qMax(52, card->sizeHint().height()) + 10;
        }
        return stack;
    }
    const int front = qMax(52, cards.last()->sizeHint().height());
    const int shown = qMin(cards.size(), 3);
    return 16 + front + 14 * (shown - 1);
}

void ToastViewport::setExpanded(bool expanded) {
    if (expanded_ == expanded) {
        return;
    }
    expanded_ = expanded;
    relayout();
    emit expandedChanged();
}

void ToastViewport::setDragging(bool dragging) {
    dragging_ = dragging;
    if (dragging_) {
        setExpanded(true);
    }
}

void ToastViewport::enterEvent(QEnterEvent* event) {
    if (!dragging_) {
        setExpanded(true);
    }
    QWidget::enterEvent(event);
}

void ToastViewport::leaveEvent(QEvent* event) {
    if (!dragging_) {
        setExpanded(false);
    }
    QWidget::leaveEvent(event);
}

void ToastViewport::relayout() {
    QWidget* parent = parentWidget();
    if (parent == nullptr) {
        return;
    }
    const auto kids = findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    int visible = 0;
    for (QWidget* child : kids) {
        if (!child->isHidden()) {
            ++visible;
        }
    }
    const int width = 420;
    const int height = visible == 0 ? 8 : qMax(heightHint(), 72);
    setAttribute(Qt::WA_TransparentForMouseEvents, visible == 0);
    const int x = parent->width() - width - 16;
    const int y = parent->height() - height - 16;
    setGeometry(qMax(8, x), qMax(8, y), width, height);
    if (visible == 0) {
        hide();
    } else {
        show();
        raise();
    }
}

void ToastViewport::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    emit expandedChanged();
}

bool ToastViewport::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        relayout();
    }
    return QWidget::eventFilter(watched, event);
}

PopupFrame::PopupFrame(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    body_ = new QVBoxLayout(this);
    body_->setContentsMargins(6, 6, 6, 6);
    body_->setSpacing(2);
}

QVBoxLayout* PopupFrame::bodyLayout() const {
    return body_;
}

void PopupFrame::popupBelow(QWidget* anchor, int gap) {
    if (anchor == nullptr) {
        return;
    }
    adjustSize();
    const QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height() + gap));
    popupAt(pos);
}

void PopupFrame::popupAt(const QPoint& globalPos) {
    adjustSize();
    move(globalPos);
    show();
    raise();
}

void PopupFrame::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(bounds, Theme::radiusMd(), Theme::radiusMd());
    painter.fillPath(path, Theme::popover());
    painter.setPen(QPen(Theme::border(), 1.0));
    painter.drawPath(path);
}

void PopupFrame::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
}

}  // namespace edgeqt::internal
