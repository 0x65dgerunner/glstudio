#pragma once

#include <QString>
#include <QWidget>
#include <functional>

class QHideEvent;
class QVBoxLayout;

namespace edgeqt {

class DropdownMenu : public QWidget {
    Q_OBJECT
public:
    explicit DropdownMenu(QWidget* parent = nullptr);

    void addLabel(const QString& text);
    void addItem(const QString& text, const std::function<void()>& onClick = {});
    void addItem(const QString& text, const QString& shortcut, const std::function<void()>& onClick = {});
    void addDisabledItem(const QString& text);
    DropdownMenu* addSubmenu(const QString& text);
    void addSeparator();
    void popupBelow(QWidget* anchor);

protected:
    void paintEvent(QPaintEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void dismiss();
    void showSubmenu(DropdownMenu* menu, QWidget* row);
    void hideSubmenu();
    void cancelHideSubmenu();
    void scheduleHideSubmenu();

    QVBoxLayout* body_ = nullptr;
    DropdownMenu* openSubmenu_ = nullptr;
    QWidget* submenuRow_ = nullptr;
    int submenuGen_ = 0;
};

class Popover : public QWidget {
    Q_OBJECT
public:
    explicit Popover(QWidget* parent = nullptr);
    QVBoxLayout* bodyLayout() const;
    void popupBelow(QWidget* anchor);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVBoxLayout* body_ = nullptr;
};

class HoverCard : public QObject {
    Q_OBJECT
public:
    explicit HoverCard(QWidget* trigger);
    void setTitle(const QString& title);
    void setDescription(const QString& description);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void showCard();
    void hideCard();
    QWidget* trigger_ = nullptr;
    QWidget* card_ = nullptr;
    QString title_;
    QString description_;
};

class Tooltip : public QObject {
    Q_OBJECT
public:
    enum class Side { Top, Bottom, Left, Right };

    static void install(QWidget* target, const QString& text, Side side = Side::Top);
};

}  // namespace edgeqt
