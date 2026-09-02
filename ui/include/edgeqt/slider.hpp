#pragma once

#include <QWidget>

namespace edgeqt {

class Slider : public QWidget {
    Q_OBJECT
public:
    explicit Slider(QWidget* parent = nullptr);
    void setRange(int min, int max);
    void setValue(int value);
    int value() const { return value_; }
    QSize sizeHint() const override;

signals:
    void valueChanged(int value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void setFromPos(int x);
    int min_ = 0;
    int max_ = 100;
    int value_ = 40;
};

}  // namespace edgeqt
