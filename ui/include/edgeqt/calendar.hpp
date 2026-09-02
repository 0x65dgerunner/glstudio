#pragma once

#include <QDate>
#include <QWidget>

namespace edgeqt {

class Calendar : public QWidget {
    Q_OBJECT
public:
    explicit Calendar(QWidget* parent = nullptr);
    QDate selectedDate() const { return selected_; }
    void setSelectedDate(const QDate& date);

signals:
    void dateSelected(const QDate& date);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void shiftMonth(int delta);
    QRect cellRect(int row, int col) const;

    QDate visible_ = QDate::currentDate();
    QDate selected_ = QDate::currentDate();
};

}  // namespace edgeqt
