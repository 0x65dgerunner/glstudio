#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QEnterEvent>
#include <QEvent>
#include <QObject>
#include <QPaintEvent>
#include <QString>

#include <functional>

class QWidget;

namespace edgeqt {

class Theme : public QObject {
    Q_OBJECT

public:
    enum class Mode { Dark, Light };

    struct Palette {
        QColor background;
        QColor foreground;
        QColor card;
        QColor cardForeground;
        QColor popover;
        QColor popoverForeground;
        QColor primary;
        QColor primaryForeground;
        QColor primaryHover;
        QColor secondary;
        QColor secondaryForeground;
        QColor muted;
        QColor mutedForeground;
        QColor accent;
        QColor accentForeground;
        QColor destructive;
        QColor destructiveForeground;
        QColor border;
        QColor input;
        QColor ring;
        QColor overlay;
        QColor hover;
        QColor scrollbar;
    };

    static Theme& instance();

    static Mode mode();
    static void setMode(Mode mode);
    static void toggle();
    static void loadSaved();

    static const Palette& palette();
    static const Palette& darkPalette();
    static const Palette& lightPalette();

    static QColor background();
    static QColor foreground();
    static QColor card();
    static QColor cardForeground();
    static QColor popover();
    static QColor popoverForeground();
    static QColor primary();
    static QColor primaryForeground();
    static QColor primaryHover();
    static QColor secondary();
    static QColor secondaryForeground();
    static QColor muted();
    static QColor mutedForeground();
    static QColor accent();
    static QColor accentForeground();
    static QColor destructive();
    static QColor destructiveForeground();
    static QColor border();
    static QColor input();
    static QColor ring();
    static QColor overlay();
    static QColor hover();
    static QColor scrollbar();
    static QColor inputFill();

    static int radiusSm();
    static int radiusMd();
    static int radiusLg();
    static int radiusXl();
    static int windowRadius();

    static int durationFast();
    static int durationSheet();
    static int durationDrawer();
    static int durationToast();

    static QString css(const QColor& color);
    static QString globalStyleSheet();
    static void apply(QWidget* root);
    static void bindStyle(QWidget* widget, std::function<QString()> stylesheet);

signals:
    void changed();

private:
    explicit Theme(QObject* parent = nullptr);
    void publish();

    Mode mode_ = Mode::Dark;
};

class ThemeToggle : public QAbstractButton {
    Q_OBJECT

public:
    explicit ThemeToggle(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {32, 32}; }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    bool hovered_ = false;
};

}  // namespace edgeqt
