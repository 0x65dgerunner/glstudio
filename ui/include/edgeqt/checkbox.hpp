#pragma once

#include <QAbstractButton>
#include <QEnterEvent>
#include <QList>

namespace edgeqt {

class Checkbox : public QAbstractButton {
    Q_OBJECT
public:
    explicit Checkbox(const QString& text = {}, QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

class Switch : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal knob READ knob WRITE setKnob)
public:
    explicit Switch(QWidget* parent = nullptr);
    qreal knob() const { return knob_; }
    void setKnob(qreal value);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void nextCheckState() override;
    void checkStateSet() override;

private:
    qreal knob_ = 0.0;
};

class Toggle : public QAbstractButton {
    Q_OBJECT
public:
    explicit Toggle(const QString& text = {}, QWidget* parent = nullptr);
    void setLeadingIcon(const QString& resourcePath);
    void setLeadingIcon(const QString& strokePath, const QString& solidPath);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QString iconPath_;
    QString solidIconPath_;
    bool hovered_ = false;
};

class Radio : public QAbstractButton {
    Q_OBJECT
public:
    explicit Radio(const QString& text = {}, QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

class RadioGroup : public QWidget {
    Q_OBJECT
public:
    explicit RadioGroup(QWidget* parent = nullptr);
    Radio* addItem(const QString& text, const QString& value = {});
    QString currentValue() const;
    void setCurrentValue(const QString& value);

signals:
    void currentChanged(const QString& value);

private:
    QList<Radio*> radios_;
};

}  // namespace edgeqt
