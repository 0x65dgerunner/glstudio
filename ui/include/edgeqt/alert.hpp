#pragma once

#include <QFrame>
#include <QString>

class QLabel;
class QVBoxLayout;

namespace edgeqt {

class Alert : public QFrame {
    Q_OBJECT
public:
    enum class Variant { Default, Destructive };
    explicit Alert(QWidget* parent = nullptr);
    void setVariant(Variant variant);
    void setTitle(const QString& title);
    void setDescription(const QString& description);
    QVBoxLayout* extraLayout() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void refreshIcon();
    Variant variant_ = Variant::Default;
    QLabel* icon_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* description_ = nullptr;
    QVBoxLayout* extra_ = nullptr;
};

}  // namespace edgeqt
