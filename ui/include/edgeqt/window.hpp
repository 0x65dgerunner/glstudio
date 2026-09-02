#pragma once

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QToolButton;
class QVBoxLayout;

namespace edgeqt {

class TitleBar : public QWidget {
    Q_OBJECT

public:
    struct Options {
        bool showMinimize = true;
        bool showMaximize = true;
        bool showClose = true;
        bool maximizeEnabled = true;
    };

    TitleBar(QWidget* window, const QString& title, const Options& options,
             QWidget* parent = nullptr);

    void setTitle(const QString& title);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void setupUi(const Options& options);
    QToolButton* makeButton(const QString& objectName);
    void updateMaximizeIcon();
    void toggleMaximize();
    void refreshTheme();

    QWidget* window_ = nullptr;
    QLabel* iconLabel_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QToolButton* minimizeButton_ = nullptr;
    QToolButton* maximizeButton_ = nullptr;
    QToolButton* closeButton_ = nullptr;
    bool maximizeEnabled_ = true;
    bool dragging_ = false;
    QPoint dragOffset_;
};

class Window : public QWidget {
    Q_OBJECT

public:
    explicit Window(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    TitleBar* titleBar() const;
    QWidget* contentWidget() const;
    QWidget* shell() const;
    QWidget* overlayHost() const;
    QWidget* toastHost() const;

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    void layoutChrome();
    void updateCornerRadius();

    QWidget* shell_ = nullptr;
    TitleBar* titleBar_ = nullptr;
    QWidget* content_ = nullptr;
    QWidget* overlayHost_ = nullptr;
    QWidget* toastHost_ = nullptr;
    QWidget* roundClip_ = nullptr;
    QWidget* resizeFrame_ = nullptr;
};

}  // namespace edgeqt
