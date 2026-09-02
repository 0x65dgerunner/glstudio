#pragma once

#include <QEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QTextEdit>

namespace edgeqt {

class Textarea : public QTextEdit {
    Q_OBJECT
public:
    explicit Textarea(QWidget* parent = nullptr);
    void setInvalid(bool invalid);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRect gripRect() const;
    void applyUserSize(const QSize& size);
    void applyClip();
    bool invalid_ = false;
    bool resizing_ = false;
    QPoint resizeOrigin_;
    QSize resizeStart_;
    QSize preferred_{400, 88};
};

}  // namespace edgeqt
