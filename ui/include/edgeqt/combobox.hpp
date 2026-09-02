#pragma once

#include <QEnterEvent>
#include <QMouseEvent>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <functional>

class QKeyEvent;
class QLineEdit;
class QResizeEvent;
class QVBoxLayout;

namespace edgeqt {

class Combobox : public QWidget {
    Q_OBJECT
public:
    explicit Combobox(QWidget* parent = nullptr);
    void addItem(const QString& text);
    void setItems(const QStringList& items);
    void setPlaceholder(const QString& text);
    QString currentText() const { return current_; }
    QSize sizeHint() const override;

signals:
    void currentTextChanged(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void openPopup();
    QStringList items_;
    QString current_;
    QString placeholder_ = QStringLiteral("Select...");
    bool hovered_ = false;
};

class Command : public QWidget {
    Q_OBJECT
public:
    explicit Command(QWidget* parent = nullptr);

    void addItem(const QString& text, const std::function<void()>& onPick = {});
    void addItem(const QString& group, const QString& text, const QString& icon,
                 const QString& shortcut = {}, const std::function<void()>& onPick = {});
    void setPlaceholder(const QString& text);
    void focusInput();
    void reset();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void itemPicked(const QString& text);
    void contentsChanged();
    void dismissed();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild();
    void relayout();
    void setSelected(int index);
    void moveSelection(int delta);
    void activateSelection();

    struct Item {
        QString group;
        QString text;
        QString icon;
        QString shortcut;
        std::function<void()> onPick;
    };
    QVector<Item> items_;
    QLineEdit* search_ = nullptr;
    QWidget* list_ = nullptr;
    QVBoxLayout* listLayout_ = nullptr;
    QVector<QWidget*> rows_;
    int selected_ = 0;
};

class CommandPalette : public QWidget {
    Q_OBJECT
public:
    explicit CommandPalette(QWidget* context);
    void addItem(const QString& text, const std::function<void()>& onPick = {});
    void addItem(const QString& group, const QString& text, const QString& icon,
                 const QString& shortcut = {}, const std::function<void()>& onPick = {});
    void setPlaceholder(const QString& text);
    void open();
    void closePopup();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void layoutOverlay();

    Command* command_ = nullptr;
    QWidget* scrim_ = nullptr;
    bool open_ = false;
};

}  // namespace edgeqt
