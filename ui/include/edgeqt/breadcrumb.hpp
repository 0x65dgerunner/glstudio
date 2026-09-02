#pragma once

#include <QWidget>
#include <QString>
#include <functional>

class QHBoxLayout;

namespace edgeqt {

class Breadcrumb : public QWidget {
    Q_OBJECT
public:
    explicit Breadcrumb(QWidget* parent = nullptr);
    void addItem(const QString& text, const std::function<void()>& onClick = {});
    void clear();

private:
    QHBoxLayout* row_ = nullptr;
    int count_ = 0;
};

}  // namespace edgeqt
