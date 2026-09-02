#pragma once

#include <QLineEdit>

namespace edgeqt {

class Input : public QLineEdit {
    Q_OBJECT

public:
    explicit Input(QWidget* parent = nullptr);

    void setInvalid(bool invalid);
    bool isInvalid() const { return invalid_; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    bool invalid_ = false;
};

}  // namespace edgeqt
