#pragma once

#include <QEasingCurve>
#include <QEnterEvent>
#include <QWidget>
#include <functional>

class QVBoxLayout;

namespace edgeqt::internal {

QEasingCurve easeOut();
QEasingCurve easeEmphasized();
QEasingCurve easeSheet();

QWidget* chromeHost(QWidget* from);
QWidget* overlayHost(QWidget* from);
QWidget* toastHost(QWidget* from);

void fadeTo(QWidget* widget, qreal target, int duration, const QEasingCurve& curve,
            const std::function<void()>& done = {});
void moveTo(QWidget* widget, const QRect& target, int duration, const QEasingCurve& curve,
            const std::function<void()>& done = {});

class Scrim : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    explicit Scrim(QWidget* parent = nullptr);

    qreal opacity() const { return opacity_; }
    void setOpacity(qreal opacity);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    qreal opacity_ = 0.0;
};

void playOverlay(Scrim* scrim, QWidget* panel, const QRect& from, const QRect& to,
                 bool opening, int duration, const QEasingCurve& curve,
                 const std::function<void()>& done = {});

class Panel : public QWidget {
    Q_OBJECT

public:
    enum class Corners { All, Top, Right, Bottom, Left, None };

    explicit Panel(QWidget* parent = nullptr);

    void setCorners(Corners corners);
    void setRadius(int radius);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Corners corners_ = Corners::All;
    int radius_ = 14;
};

class OverlayHost : public QWidget {
    Q_OBJECT

public:
    explicit OverlayHost(QWidget* parent = nullptr);
    void refreshPassthrough();

protected:
    void childEvent(QChildEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
};

class ToastViewport : public QWidget {
    Q_OBJECT

public:
    explicit ToastViewport(QWidget* parent = nullptr);
    void relayout();
    int heightHint() const;
    void setExpanded(bool expanded);
    bool isExpanded() const { return expanded_; }
    void setDragging(bool dragging);

signals:
    void expandedChanged();

protected:
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    bool expanded_ = false;
    bool dragging_ = false;
};

class PopupFrame : public QWidget {
    Q_OBJECT

public:
    explicit PopupFrame(QWidget* parent = nullptr);
    QVBoxLayout* bodyLayout() const;
    void popupBelow(QWidget* anchor, int gap = 6);
    void popupAt(const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QVBoxLayout* body_ = nullptr;
};

}  // namespace edgeqt::internal
