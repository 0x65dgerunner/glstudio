#pragma once

#include <QWidget>
#include <QString>

class QHBoxLayout;
class QHideEvent;
class QPropertyAnimation;
class QVBoxLayout;

namespace edgeqt {

class Empty : public QWidget {
    Q_OBJECT
public:
    explicit Empty(QWidget* parent = nullptr);
    void setIcon(const QString& strokeName);
    void setTitle(const QString& title);
    void setDescription(const QString& description);
    QHBoxLayout* actionLayout() const;
    QVBoxLayout* footerLayout() const;

private:
    void refreshIcon();
    QWidget* media_ = nullptr;
    QString iconName_ = QStringLiteral("folder-code");
    QWidget* titleLabel_ = nullptr;
    QWidget* descriptionLabel_ = nullptr;
    QHBoxLayout* actions_ = nullptr;
    QVBoxLayout* footer_ = nullptr;
};

class Kbd : public QWidget {
    Q_OBJECT
public:
    explicit Kbd(const QString& text = {}, QWidget* parent = nullptr);
    void setText(const QString& text);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString text_;
};

class Label : public QWidget {
    Q_OBJECT
public:
    explicit Label(const QString& text = {}, QWidget* parent = nullptr);
    void setText(const QString& text);
    void setMuted(bool muted);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString text_;
    bool muted_ = false;
};

class Separator : public QWidget {
    Q_OBJECT
public:
    explicit Separator(Qt::Orientation orientation = Qt::Horizontal, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

class Skeleton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal phase READ phase WRITE setPhase)
public:
    explicit Skeleton(QWidget* parent = nullptr);
    qreal phase() const { return phase_; }
    void setPhase(qreal value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    qreal phase_ = 0.0;
    QPropertyAnimation* anim_ = nullptr;
};

class Progress : public QWidget {
    Q_OBJECT
public:
    explicit Progress(QWidget* parent = nullptr);
    void setValue(int value);
    int value() const { return value_; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int value_ = 0;
};

}  // namespace edgeqt
