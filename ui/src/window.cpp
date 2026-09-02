#include <edgeqt/window.hpp>

#include "internal.hpp"

#include <edgeqt/fonts.hpp>
#include <edgeqt/icons.hpp>
#include <edgeqt/theme.hpp>

#include <QAbstractButton>
#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QHoverEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRegion>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyleFactory>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_DONOTROUND 1
#define DWMWCP_ROUND 2
#endif
#endif

namespace edgeqt {
namespace {

constexpr int kTitleBarHeight = 36;
constexpr int kResizeBorder = 8;
constexpr int kResizeCorner = 20;

bool useCompositorRounding() {
    return false;
}

void applyCompositorRounding(QWidget* window, bool) {
#ifdef Q_OS_WIN
    if (window == nullptr) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd == nullptr) {
        return;
    }
    const DWORD preference = static_cast<DWORD>(DWMWCP_DONOTROUND);
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
#else
    Q_UNUSED(window);
#endif
}

#ifdef Q_OS_WIN
LPCWSTR resizeCursorAt(HWND hwnd, POINT screen, qreal dpr) {
    RECT winRect{};
    GetWindowRect(hwnd, &winRect);
    const int edge = qMax(kResizeBorder, qRound(kResizeBorder * dpr));
    const int corner = qMax(kResizeCorner, qRound(kResizeCorner * dpr));
    const long x = screen.x;
    const long y = screen.y;

    const bool leftEdge = x >= winRect.left && x < winRect.left + edge;
    const bool rightEdge = x <= winRect.right && x > winRect.right - edge;
    const bool topEdge = y >= winRect.top && y < winRect.top + edge;
    const bool bottomEdge = y <= winRect.bottom && y > winRect.bottom - edge;
    const bool leftCorner = x >= winRect.left && x < winRect.left + corner;
    const bool rightCorner = x <= winRect.right && x > winRect.right - corner;
    const bool topCorner = y >= winRect.top && y < winRect.top + corner;
    const bool bottomCorner = y <= winRect.bottom && y > winRect.bottom - corner;

    if ((topCorner && leftCorner) || (bottomCorner && rightCorner)) {
        return IDC_SIZENWSE;
    }
    if ((topCorner && rightCorner) || (bottomCorner && leftCorner)) {
        return IDC_SIZENESW;
    }
    if (leftEdge || rightEdge) {
        return IDC_SIZEWE;
    }
    if (topEdge || bottomEdge) {
        return IDC_SIZENS;
    }
    return nullptr;
}
#endif

class RoundedShell : public QWidget {
public:
    explicit RoundedShell(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("RoundedWindowShell"));
        setAttribute(Qt::WA_StyledBackground, false);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAutoFillBackground(false);
    }

    void setCornerRadius(int radius) {
        if (cornerRadius_ == radius) {
            return;
        }
        cornerRadius_ = radius;
        update();
    }

    int cornerRadius() const { return cornerRadius_; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath fill;
        if (cornerRadius_ > 0) {
            fill.addRoundedRect(bounds, cornerRadius_, cornerRadius_);
        } else {
            fill.addRect(bounds);
        }
        painter.fillPath(fill, Theme::background());
    }

private:
    int cornerRadius_ = Theme::windowRadius();
};

class RoundClip : public QWidget {
public:
    explicit RoundClip(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName(QStringLiteral("RoundClip"));
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        setAutoFillBackground(false);
    }

    void setRadius(int radius) {
        if (radius_ == radius) {
            return;
        }
        radius_ = radius;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        if (radius_ <= 0 || width() <= 0 || height() <= 0) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QRectF bounds = QRectF(rect());
        QPainterPath outer;
        outer.addRect(bounds);
        QPainterPath inner;
        inner.addRoundedRect(bounds.adjusted(0.5, 0.5, -0.5, -0.5), radius_, radius_);

        painter.setPen(Qt::NoPen);
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillPath(outer.subtracted(inner), Qt::transparent);

        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.setBrush(Qt::NoBrush);

        QPen edge(Theme::background(), 1.5);
        edge.setCosmetic(true);
        edge.setJoinStyle(Qt::RoundJoin);
        painter.setPen(edge);
        painter.drawPath(inner);

        QPen border(Theme::border(), 1.0);
        border.setCosmetic(true);
        border.setJoinStyle(Qt::RoundJoin);
        painter.setPen(border);
        painter.drawPath(inner);
    }

private:
    int radius_ = Theme::windowRadius();
};

class ResizeFrame : public QWidget {
public:
    explicit ResizeFrame(QWidget* window) : QWidget(window), window_(window) {
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_Hover, true);
        setMouseTracking(true);
        setFocusPolicy(Qt::NoFocus);
        setAutoFillBackground(false);
    }

    void paintEvent(QPaintEvent*) override {}

    void refreshMask() {
        if (window_ != nullptr && window_->isMaximized()) {
            clearMask();
            hide();
            return;
        }
        if (width() < kResizeCorner * 2 || height() < kResizeCorner * 2) {
            return;
        }
        show();
        QRegion region;
        region += QRect(0, 0, width(), kResizeBorder);
        region += QRect(0, height() - kResizeBorder, width(), kResizeBorder);
        region += QRect(0, 0, kResizeBorder, height());
        region += QRect(width() - kResizeBorder, 0, kResizeBorder, height());
        region += QRect(0, 0, kResizeCorner, kResizeCorner);
        region += QRect(width() - kResizeCorner, 0, kResizeCorner, kResizeCorner);
        region += QRect(0, height() - kResizeCorner, kResizeCorner, kResizeCorner);
        region += QRect(width() - kResizeCorner, height() - kResizeCorner, kResizeCorner,
                        kResizeCorner);
        setMask(region);
        raise();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        refreshMask();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || window_ == nullptr || window_->isMaximized()) {
            QWidget::mousePressEvent(event);
            return;
        }
        edges_ = edgesAt(event->pos());
        if (edges_ == Qt::Edges()) {
            QWidget::mousePressEvent(event);
            return;
        }
        dragging_ = true;
        origin_ = event->globalPosition().toPoint();
        startGeometry_ = window_->geometry();
        grabMouse();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging_ && window_ != nullptr) {
            applyResize(event->globalPosition().toPoint());
            event->accept();
            return;
        }
        applyCursor(edgesAt(event->pos()));
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && dragging_) {
            dragging_ = false;
            edges_ = {};
            releaseMouse();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    bool event(QEvent* event) override {
        if (!dragging_ && event->type() == QEvent::HoverMove) {
            const auto* hover = static_cast<QHoverEvent*>(event);
            applyCursor(edgesAt(hover->position().toPoint()));
        } else if (!dragging_ && event->type() == QEvent::HoverLeave) {
            unsetCursor();
        }
        return QWidget::event(event);
    }

    void leaveEvent(QEvent* event) override {
        if (!dragging_) {
            unsetCursor();
        }
        QWidget::leaveEvent(event);
    }

private:
    Qt::Edges edgesAt(const QPoint& pos) const {
        Qt::Edges edges;
        const bool nearLeft = pos.x() <= kResizeCorner;
        const bool nearRight = pos.x() >= width() - kResizeCorner;
        const bool nearTop = pos.y() <= kResizeCorner;
        const bool nearBottom = pos.y() >= height() - kResizeCorner;
        const bool inCorner = (nearLeft || nearRight) && (nearTop || nearBottom);

        if (pos.x() <= kResizeBorder || (inCorner && nearLeft)) {
            edges |= Qt::LeftEdge;
        }
        if (pos.x() >= width() - kResizeBorder || (inCorner && nearRight)) {
            edges |= Qt::RightEdge;
        }
        if (pos.y() <= kResizeBorder || (inCorner && nearTop)) {
            edges |= Qt::TopEdge;
        }
        if (pos.y() >= height() - kResizeBorder || (inCorner && nearBottom)) {
            edges |= Qt::BottomEdge;
        }
        return edges;
    }

    void applyCursor(Qt::Edges edges) {
        if (edges == (Qt::TopEdge | Qt::LeftEdge) || edges == (Qt::BottomEdge | Qt::RightEdge)) {
            setCursor(Qt::SizeFDiagCursor);
        } else if (edges == (Qt::TopEdge | Qt::RightEdge) || edges == (Qt::BottomEdge | Qt::LeftEdge)) {
            setCursor(Qt::SizeBDiagCursor);
        } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
            setCursor(Qt::SizeHorCursor);
        } else if (edges & (Qt::TopEdge | Qt::BottomEdge)) {
            setCursor(Qt::SizeVerCursor);
        } else {
            unsetCursor();
        }
    }

    void applyResize(const QPoint& globalPos) {
        QRect geom = startGeometry_;
        const QPoint delta = globalPos - origin_;
        if (edges_ & Qt::LeftEdge) {
            geom.setLeft(startGeometry_.left() + delta.x());
        }
        if (edges_ & Qt::RightEdge) {
            geom.setRight(startGeometry_.right() + delta.x());
        }
        if (edges_ & Qt::TopEdge) {
            geom.setTop(startGeometry_.top() + delta.y());
        }
        if (edges_ & Qt::BottomEdge) {
            geom.setBottom(startGeometry_.bottom() + delta.y());
        }

        const QSize minSize = window_->minimumSize();
        const QSize maxSize = window_->maximumSize();
        if (geom.width() < minSize.width()) {
            if (edges_ & Qt::LeftEdge) {
                geom.setLeft(geom.right() - minSize.width());
            } else {
                geom.setWidth(minSize.width());
            }
        }
        if (geom.height() < minSize.height()) {
            if (edges_ & Qt::TopEdge) {
                geom.setTop(geom.bottom() - minSize.height());
            } else {
                geom.setHeight(minSize.height());
            }
        }
        if (maxSize.width() < QWIDGETSIZE_MAX && geom.width() > maxSize.width()) {
            if (edges_ & Qt::LeftEdge) {
                geom.setLeft(geom.right() - maxSize.width());
            } else {
                geom.setWidth(maxSize.width());
            }
        }
        if (maxSize.height() < QWIDGETSIZE_MAX && geom.height() > maxSize.height()) {
            if (edges_ & Qt::TopEdge) {
                geom.setTop(geom.bottom() - maxSize.height());
            } else {
                geom.setHeight(maxSize.height());
            }
        }

        window_->setGeometry(geom);
    }

    QWidget* window_ = nullptr;
    bool dragging_ = false;
    Qt::Edges edges_;
    QPoint origin_;
    QRect startGeometry_;
};

QString titleBarStyleSheet() {
    return QStringLiteral(
               "edgeqt--TitleBar {"
               "  background: transparent;"
               "  border-bottom: 1px solid %1;"
               "}"
               "QLabel#TitleBarIcon { background: transparent; }"
               "QLabel#TitleBarLabel {"
               "  color: %2;"
               "  background: transparent;"
               "}"
               "QToolButton#TitleBarButton, QToolButton#TitleBarCloseButton {"
               "  border: none;"
               "  border-radius: 4px;"
               "  padding: 0;"
               "  min-width: 36px;"
               "  max-width: 36px;"
               "  min-height: 32px;"
               "  max-height: 32px;"
               "  background: transparent;"
               "}"
               "QToolButton#TitleBarButton:hover { background-color: %3; }"
               "QToolButton#TitleBarCloseButton:hover { background-color: #c42b1c; }")
        .arg(Theme::css(Theme::border()), Theme::css(Theme::mutedForeground()),
             Theme::css(Theme::hover()));
}

}  // namespace

TitleBar::TitleBar(QWidget* window, const QString& title, const Options& options, QWidget* parent)
    : QWidget(parent), window_(window), maximizeEnabled_(options.maximizeEnabled) {
    setObjectName(QStringLiteral("edgeqt--TitleBar"));
    setFixedHeight(kTitleBarHeight);
    setStyleSheet(titleBarStyleSheet());
    setupUi(options);
    setTitle(title);
    if (window_ != nullptr) {
        window_->installEventFilter(this);
    }
    connect(&Theme::instance(), &Theme::changed, this, &TitleBar::refreshTheme);
}

void TitleBar::setupUi(const Options& options) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(10, 0, 4, 0);
    row->setSpacing(0);

    iconLabel_ = new QLabel(this);
    iconLabel_->setObjectName(QStringLiteral("TitleBarIcon"));
    iconLabel_->setFixedSize(24, 32);
    iconLabel_->setAlignment(Qt::AlignCenter);
    iconLabel_->setPixmap(Icons::pixmap(Icons::logo(), 16, Theme::mutedForeground(),
                                        devicePixelRatioF()));
    row->addWidget(iconLabel_);

    auto* gap = new QWidget(this);
    gap->setFixedWidth(6);
    gap->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(gap);

    titleLabel_ = new QLabel(this);
    titleLabel_->setObjectName(QStringLiteral("TitleBarLabel"));
    titleLabel_->setFont(Fonts::regular(9.0));
    row->addWidget(titleLabel_, 1);

    auto* themeToggle = new ThemeToggle(this);
    row->addWidget(themeToggle);

    const QColor iconColor = Theme::mutedForeground();

    if (options.showMinimize) {
        minimizeButton_ = makeButton(QStringLiteral("TitleBarButton"));
        Icons::applyHoverIcon(minimizeButton_, Icons::stroke(QStringLiteral("minimize")),
                              Icons::solid(QStringLiteral("minimize")), iconColor);
        connect(minimizeButton_, &QToolButton::clicked, window_, [this]() {
            if (window_ != nullptr) {
                window_->showMinimized();
            }
        });
        row->addWidget(minimizeButton_);
    }

    if (options.showMaximize) {
        maximizeButton_ = makeButton(QStringLiteral("TitleBarButton"));
        if (options.maximizeEnabled) {
            updateMaximizeIcon();
            connect(maximizeButton_, &QToolButton::clicked, this, &TitleBar::toggleMaximize);
        } else {
            maximizeButton_->setEnabled(false);
            maximizeButton_->setIcon(
                Icons::icon(Icons::stroke(QStringLiteral("maximize")), Theme::muted()));
        }
        row->addWidget(maximizeButton_);
    }

    if (options.showClose) {
        closeButton_ = makeButton(QStringLiteral("TitleBarCloseButton"));
        Icons::applyHoverIcon(closeButton_, Icons::stroke(QStringLiteral("close")),
                              Icons::solid(QStringLiteral("close")), iconColor);
        connect(closeButton_, &QToolButton::clicked, window_, [this]() {
            if (window_ != nullptr) {
                window_->close();
            }
        });
        row->addWidget(closeButton_);
    }
}

QToolButton* TitleBar::makeButton(const QString& objectName) {
    auto* button = new QToolButton(this);
    button->setObjectName(objectName);
    button->setCursor(Qt::ArrowCursor);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIconSize(QSize(14, 14));
    return button;
}

void TitleBar::setTitle(const QString& title) {
    if (titleLabel_ != nullptr) {
        titleLabel_->setText(title);
    }
    if (window_ != nullptr) {
        window_->setWindowTitle(title);
    }
}

bool TitleBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window_ && event->type() == QEvent::WindowStateChange) {
        updateMaximizeIcon();
    }
    return QWidget::eventFilter(watched, event);
}

void TitleBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && window_ != nullptr) {
        if (QWidget* child = childAt(event->pos()); child != nullptr) {
            if (qobject_cast<QAbstractButton*>(child) != nullptr) {
                QWidget::mousePressEvent(event);
                return;
            }
        }
        dragging_ = true;
        dragOffset_ = event->globalPosition().toPoint() - window_->frameGeometry().topLeft();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_ && window_ != nullptr && (event->buttons() & Qt::LeftButton)) {
        if (window_->isMaximized()) {
            const double ratio = static_cast<double>(event->pos().x()) / static_cast<double>(width());
            window_->showNormal();
            dragOffset_.setX(static_cast<int>(window_->width() * ratio));
            dragOffset_.setY(event->pos().y());
        }
        window_->move(event->globalPosition().toPoint() - dragOffset_);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void TitleBar::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && maximizeButton_ != nullptr && maximizeEnabled_) {
        toggleMaximize();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TitleBar::updateMaximizeIcon() {
    if (maximizeButton_ == nullptr || window_ == nullptr) {
        return;
    }
    const bool maximized = window_->isMaximized();
    const QColor iconColor = Theme::mutedForeground();
    Icons::applyHoverIcon(maximizeButton_,
                          Icons::stroke(maximized ? QStringLiteral("restore") : QStringLiteral("maximize")),
                          Icons::solid(maximized ? QStringLiteral("restore") : QStringLiteral("maximize")),
                          iconColor);
}

void TitleBar::refreshTheme() {
    setStyleSheet(titleBarStyleSheet());
    if (iconLabel_ != nullptr) {
        iconLabel_->setPixmap(Icons::pixmap(Icons::logo(), 16, Theme::mutedForeground(),
                                            devicePixelRatioF()));
    }
    const QColor iconColor = Theme::mutedForeground();
    if (minimizeButton_ != nullptr) {
        Icons::applyHoverIcon(minimizeButton_, Icons::stroke(QStringLiteral("minimize")),
                              Icons::solid(QStringLiteral("minimize")), iconColor);
    }
    if (closeButton_ != nullptr) {
        Icons::applyHoverIcon(closeButton_, Icons::stroke(QStringLiteral("close")),
                              Icons::solid(QStringLiteral("close")), iconColor);
    }
    if (maximizeButton_ != nullptr && !maximizeEnabled_) {
        maximizeButton_->setIcon(
            Icons::icon(Icons::stroke(QStringLiteral("maximize")), Theme::muted()));
    } else {
        updateMaximizeIcon();
    }
    update();
}

void TitleBar::toggleMaximize() {
    if (window_ == nullptr) {
        return;
    }
    if (window_->isMaximized()) {
        window_->showNormal();
    } else {
        window_->showMaximized();
    }
    updateMaximizeIcon();
}

Window::Window(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint |
                   Qt::WindowSystemMenuHint | Qt::WindowMinimizeButtonHint |
                   Qt::WindowMaximizeButtonHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    Theme::apply(this);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* shell = new RoundedShell(this);
    shell_ = shell;
    auto* shellLayout = new QVBoxLayout(shell_);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);

    TitleBar::Options options;
    titleBar_ = new TitleBar(this, QStringLiteral("edgeqt"), options, shell_);
    shellLayout->addWidget(titleBar_);

    content_ = new QWidget(shell_);
    content_->setObjectName(QStringLiteral("WindowContent"));
    content_->setAttribute(Qt::WA_StyledBackground, false);
    content_->setAutoFillBackground(false);
    content_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    shellLayout->addWidget(content_, 1);

    outer->addWidget(shell_, 1);

    overlayHost_ = new internal::OverlayHost(shell_);
    overlayHost_->setObjectName(QStringLiteral("ShOverlayHost"));

    // Parent to the window, not the shell layout, so the toast stack stays above
    // content and overlays. Unmanaged children of a laid-out shell can end up
    // behind the content widget on Windows.
    toastHost_ = new internal::ToastViewport(this);
    toastHost_->setObjectName(QStringLiteral("ShToastViewport"));
    toastHost_->setAttribute(Qt::WA_NoSystemBackground, true);
    toastHost_->setAutoFillBackground(false);

    roundClip_ = new RoundClip(this);

    auto* resizeFrame = new ResizeFrame(this);
    resizeFrame_ = resizeFrame;

    layoutChrome();
}

void Window::setTitle(const QString& title) {
    if (titleBar_ != nullptr) {
        titleBar_->setTitle(title);
    }
}

TitleBar* Window::titleBar() const {
    return titleBar_;
}

QWidget* Window::contentWidget() const {
    return content_;
}

QWidget* Window::shell() const {
    return shell_;
}

QWidget* Window::overlayHost() const {
    return overlayHost_;
}

QWidget* Window::toastHost() const {
    return toastHost_;
}

void Window::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    layoutChrome();
}

void Window::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layoutChrome();
}

void Window::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange) {
        updateCornerRadius();
        layoutChrome();
    }
    QWidget::changeEvent(event);
}

void Window::layoutChrome() {
    if (overlayHost_ != nullptr && shell_ != nullptr) {
        overlayHost_->setGeometry(shell_->rect());
        overlayHost_->raise();
    }
    if (auto* viewport = qobject_cast<internal::ToastViewport*>(toastHost_)) {
        viewport->relayout();
        viewport->raise();
    }
    if (auto* clip = static_cast<RoundClip*>(roundClip_)) {
        clip->setGeometry(rect());
        clip->setRadius(isMaximized() ? 0 : Theme::windowRadius());
        clip->raise();
    }
    if (auto* frame = static_cast<ResizeFrame*>(resizeFrame_)) {
        frame->setGeometry(rect());
        frame->refreshMask();
        frame->raise();
    }
    updateCornerRadius();
}

void Window::updateCornerRadius() {
    const bool round = !isMaximized();
    if (auto* shell = static_cast<RoundedShell*>(shell_)) {
        shell->setCornerRadius(round ? Theme::windowRadius() : 0);
    }
    applyCompositorRounding(this, round);
}

#ifdef Q_OS_WIN
bool Window::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
    if (message == nullptr || result == nullptr) {
        return QWidget::nativeEvent(eventType, message, result);
    }
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
        return QWidget::nativeEvent(eventType, message, result);
    }

    const auto* msg = static_cast<MSG*>(message);
    if (msg->message == WM_SETCURSOR && !isMaximized()) {
        POINT cursorPos{};
        GetCursorPos(&cursorPos);
        if (LPCWSTR cursorId =
                resizeCursorAt(reinterpret_cast<HWND>(internalWinId()), cursorPos, devicePixelRatioF())) {
            SetCursor(LoadCursor(nullptr, cursorId));
            *result = TRUE;
            return true;
        }
    }

    return QWidget::nativeEvent(eventType, message, result);
}
#endif

}  // namespace edgeqt
