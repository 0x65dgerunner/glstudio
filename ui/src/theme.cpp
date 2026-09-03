#include <edgeqt/theme.hpp>

#include <edgeqt/fonts.hpp>
#include <edgeqt/icons.hpp>

#include <QApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPointer>
#include <QScrollBar>
#include <QSettings>
#include <QStyleFactory>
#include <QToolTip>
#include <QWidget>
#include <QVector>

namespace edgeqt {
namespace {

struct StyleBinding {
    QPointer<QWidget> widget;
    std::function<QString()> stylesheet;
};

QVector<StyleBinding>& styleBindings() {
    static QVector<StyleBinding> bindings;
    return bindings;
}

Theme::Palette makeDark() {
    Theme::Palette p;
    p.background = QColor(QStringLiteral("#0a0a0a"));
    p.foreground = QColor(QStringLiteral("#e8e8ea"));
    p.card = QColor(QStringLiteral("#141418"));
    p.cardForeground = QColor(QStringLiteral("#ececef"));
    p.popover = QColor(QStringLiteral("#141418"));
    p.popoverForeground = QColor(QStringLiteral("#e8e8ea"));
    p.primary = QColor(QStringLiteral("#2a5fff"));
    p.primaryForeground = QColor(QStringLiteral("#ffffff"));
    p.primaryHover = QColor(QStringLiteral("#3d7cff"));
    p.secondary = QColor(QStringLiteral("#26262b"));
    p.secondaryForeground = QColor(QStringLiteral("#e8e8ea"));
    p.muted = QColor(QStringLiteral("#26262b"));
    p.mutedForeground = QColor(QStringLiteral("#a1a1aa"));
    p.accent = QColor(QStringLiteral("#1f1f23"));
    p.accentForeground = QColor(QStringLiteral("#e8e8ea"));
    p.destructive = QColor(QStringLiteral("#f87171"));
    p.destructiveForeground = QColor(QStringLiteral("#f87171"));
    p.border = QColor(QStringLiteral("#2c2c30"));
    p.input = QColor(QStringLiteral("#2c2c30"));
    p.ring = QColor(QStringLiteral("#3d7cff"));
    p.overlay = QColor(0, 0, 0, 110);
    p.hover = QColor(QStringLiteral("#1f1f23"));
    p.scrollbar = QColor(255, 255, 255, 42);
    return p;
}

Theme::Palette makeLight() {
    Theme::Palette p;
    p.background = QColor(QStringLiteral("#f4f4f5"));
    p.foreground = QColor(QStringLiteral("#09090b"));
    p.card = QColor(QStringLiteral("#ffffff"));
    p.cardForeground = QColor(QStringLiteral("#09090b"));
    p.popover = QColor(QStringLiteral("#ffffff"));
    p.popoverForeground = QColor(QStringLiteral("#09090b"));
    p.primary = QColor(QStringLiteral("#2a5fff"));
    p.primaryForeground = QColor(QStringLiteral("#ffffff"));
    p.primaryHover = QColor(QStringLiteral("#1e4fe0"));
    p.secondary = QColor(QStringLiteral("#e4e4e7"));
    p.secondaryForeground = QColor(QStringLiteral("#09090b"));
    p.muted = QColor(QStringLiteral("#e4e4e7"));
    p.mutedForeground = QColor(QStringLiteral("#71717a"));
    p.accent = QColor(QStringLiteral("#e4e4e7"));
    p.accentForeground = QColor(QStringLiteral("#09090b"));
    p.destructive = QColor(QStringLiteral("#dc2626"));
    p.destructiveForeground = QColor(QStringLiteral("#dc2626"));
    p.border = QColor(QStringLiteral("#e4e4e7"));
    p.input = QColor(QStringLiteral("#d4d4d8"));
    p.ring = QColor(QStringLiteral("#2a5fff"));
    p.overlay = QColor(15, 15, 17, 90);
    p.hover = QColor(QStringLiteral("#e4e4e7"));
    p.scrollbar = QColor(24, 24, 27, 48);
    return p;
}

void applyColors(QPalette& palette) {
    palette.setColor(QPalette::Window, Theme::background());
    palette.setColor(QPalette::Base, Theme::background());
    palette.setColor(QPalette::AlternateBase, Theme::card());
    palette.setColor(QPalette::Button, Theme::secondary());
    palette.setColor(QPalette::WindowText, Theme::foreground());
    palette.setColor(QPalette::Text, Theme::foreground());
    palette.setColor(QPalette::ButtonText, Theme::foreground());
    palette.setColor(QPalette::BrightText, Theme::foreground());
    palette.setColor(QPalette::PlaceholderText, Theme::mutedForeground());
    palette.setColor(QPalette::Highlight, Theme::primary());
    palette.setColor(QPalette::HighlightedText, Theme::primaryForeground());
    palette.setColor(QPalette::Light, Theme::card());
    palette.setColor(QPalette::Mid, Theme::border());
    palette.setColor(QPalette::Dark, Theme::background());
}

void applyPaletteTo(QWidget* root) {
    if (root == nullptr) {
        return;
    }
    QPalette palette = root->palette();
    applyColors(palette);
    root->setPalette(palette);
}

}  // namespace

Theme& Theme::instance() {
    static Theme theme;
    return theme;
}

Theme::Theme(QObject* parent) : QObject(parent) {
    QSettings settings;
    const QString saved = settings.value(QStringLiteral("theme"), QStringLiteral("dark")).toString();
    mode_ = saved == QStringLiteral("light") ? Mode::Light : Mode::Dark;
}

Theme::Mode Theme::mode() {
    return instance().mode_;
}

void Theme::setMode(Mode mode) {
    if (instance().mode_ == mode) {
        return;
    }
    instance().mode_ = mode;
    instance().publish();
    QSettings settings;
    settings.setValue(QStringLiteral("theme"),
                      mode == Mode::Light ? QStringLiteral("light") : QStringLiteral("dark"));
}

void Theme::toggle() {
    setMode(mode() == Mode::Dark ? Mode::Light : Mode::Dark);
}

void Theme::loadSaved() {
    instance().publish();
}

const Theme::Palette& Theme::darkPalette() {
    static const Palette palette = makeDark();
    return palette;
}

const Theme::Palette& Theme::lightPalette() {
    static const Palette palette = makeLight();
    return palette;
}

const Theme::Palette& Theme::palette() {
    return mode() == Mode::Light ? lightPalette() : darkPalette();
}

QColor Theme::background() { return palette().background; }
QColor Theme::foreground() { return palette().foreground; }
QColor Theme::card() { return palette().card; }
QColor Theme::cardForeground() { return palette().cardForeground; }
QColor Theme::popover() { return palette().popover; }
QColor Theme::popoverForeground() { return palette().popoverForeground; }
QColor Theme::primary() { return palette().primary; }
QColor Theme::primaryForeground() { return palette().primaryForeground; }
QColor Theme::primaryHover() { return palette().primaryHover; }
QColor Theme::secondary() { return palette().secondary; }
QColor Theme::secondaryForeground() { return palette().secondaryForeground; }
QColor Theme::muted() { return palette().muted; }
QColor Theme::mutedForeground() { return palette().mutedForeground; }
QColor Theme::accent() { return palette().accent; }
QColor Theme::accentForeground() { return palette().accentForeground; }
QColor Theme::destructive() { return palette().destructive; }
QColor Theme::destructiveForeground() { return palette().destructiveForeground; }
QColor Theme::border() { return palette().border; }
QColor Theme::input() { return palette().input; }
QColor Theme::ring() { return palette().ring; }
QColor Theme::overlay() { return palette().overlay; }
QColor Theme::hover() { return palette().hover; }
QColor Theme::scrollbar() { return palette().scrollbar; }

QColor Theme::inputFill() {
    return mode() == Mode::Light ? popover() : background();
}

int Theme::radiusSm() { return 6; }
int Theme::radiusMd() { return 8; }
int Theme::radiusLg() { return 10; }
int Theme::radiusXl() { return 14; }
int Theme::windowRadius() { return 10; }

int Theme::durationFast() { return 120; }
int Theme::durationSheet() { return 220; }
int Theme::durationDrawer() { return 450; }
int Theme::durationToast() { return 500; }

QString Theme::css(const QColor& color) {
    if (color.alpha() >= 255) {
        return color.name(QColor::HexRgb);
    }
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alphaF(), 0, 'f', 3);
}

QString Theme::globalStyleSheet() {
    return QStringLiteral(
               "QScrollArea { background: transparent; border: none; }"
               "QAbstractScrollArea::viewport { background: transparent; }"
               "QStackedWidget { background: transparent; }"
               "QScrollBar:vertical {"
               "  background: transparent;"
               "  width: 6px;"
               "  margin: 8px 3px 8px 0px;"
               "}"
               "QScrollBar::handle:vertical {"
               "  background: %1;"
               "  min-height: 28px;"
               "  border-radius: 3px;"
               "}"
               "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
               "  height: 0;"
               "  width: 0;"
               "  background: none;"
               "}"
               "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
               "  background: none;"
               "}"
               "QScrollBar:horizontal {"
               "  background: transparent;"
               "  height: 6px;"
               "  margin: 0px 8px 3px 8px;"
               "}"
               "QScrollBar::handle:horizontal {"
               "  background: %1;"
               "  min-width: 28px;"
               "  border-radius: 3px;"
               "}"
               "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
               "  height: 0;"
               "  width: 0;"
               "  background: none;"
               "}"
               "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
               "  background: none;"
               "}"
               "QToolTip {"
               "  background-color: %2;"
               "  color: %3;"
               "  border: 1px solid %4;"
               "  border-radius: 6px;"
               "  padding: 4px 8px;"
               "}")
        .arg(css(scrollbar()), css(popover()), css(foreground()), css(border()));
}

void Theme::apply(QWidget* root) {
    if (root == nullptr) {
        return;
    }
    applyPaletteTo(root);
    root->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    root->setStyleSheet(globalStyleSheet());
}

void Theme::bindStyle(QWidget* widget, std::function<QString()> stylesheet) {
    if (widget == nullptr || !stylesheet) {
        return;
    }
    auto& bindings = styleBindings();
    for (StyleBinding& binding : bindings) {
        if (binding.widget == widget) {
            binding.stylesheet = std::move(stylesheet);
            widget->setStyleSheet(binding.stylesheet());
            return;
        }
    }
    widget->setStyleSheet(stylesheet());
    bindings.push_back({widget, std::move(stylesheet)});
}

void Theme::publish() {
    QPalette tipPalette;
    if (qApp != nullptr) {
        QPalette appPalette = qApp->palette();
        applyColors(appPalette);
        qApp->setPalette(appPalette);
        tipPalette = QToolTip::palette();
        tipPalette.setColor(QPalette::ToolTipBase, popover());
        tipPalette.setColor(QPalette::ToolTipText, foreground());
        QToolTip::setPalette(tipPalette);
    }

    const auto topLevels = qApp != nullptr ? qApp->topLevelWidgets() : QWidgetList();
    for (QWidget* window : topLevels) {
        window->setUpdatesEnabled(false);
        applyPaletteTo(window);
    }

    auto& bindings = styleBindings();
    for (int i = bindings.size() - 1; i >= 0; --i) {
        QWidget* widget = bindings.at(i).widget;
        if (widget == nullptr) {
            bindings.removeAt(i);
            continue;
        }
        widget->setStyleSheet(bindings.at(i).stylesheet());
    }

    const QString scrollCss = QStringLiteral(
                                  "QScrollBar:vertical { background: transparent; width: 6px; margin: 8px 3px 8px 0px; }"
                                  "QScrollBar::handle:vertical { background: %1; min-height: 28px; border-radius: 3px; }"
                                  "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; width: 0; background: none; }"
                                  "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
                                  "QScrollBar:horizontal { background: transparent; height: 6px; margin: 0px 8px 3px 8px; }"
                                  "QScrollBar::handle:horizontal { background: %1; min-width: 28px; border-radius: 3px; }"
                                  "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { height: 0; width: 0; background: none; }"
                                  "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }")
                                  .arg(css(scrollbar()));

    for (QWidget* window : topLevels) {
        const auto bars = window->findChildren<QScrollBar*>();
        for (QScrollBar* bar : bars) {
            bar->setStyleSheet(scrollCss);
        }
    }

    emit changed();

    for (QWidget* window : topLevels) {
        window->setUpdatesEnabled(true);
    }
    if (qApp != nullptr) {
        const auto widgets = qApp->allWidgets();
        for (QWidget* widget : widgets) {
            widget->update();
        }
    }
}

ThemeToggle::ThemeToggle(QWidget* parent) : QAbstractButton(parent) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(32, 32);
    setAttribute(Qt::WA_Hover, true);
    setToolTip(QStringLiteral("Toggle theme"));
    connect(this, &QAbstractButton::clicked, this, []() { Theme::toggle(); });
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        setToolTip(Theme::mode() == Theme::Mode::Dark ? QStringLiteral("Switch to light theme")
                                                      : QStringLiteral("Switch to dark theme"));
        update();
    });
    setToolTip(Theme::mode() == Theme::Mode::Dark ? QStringLiteral("Switch to light theme")
                                                  : QStringLiteral("Switch to dark theme"));
}

void ThemeToggle::enterEvent(QEnterEvent* event) {
    hovered_ = true;
    update();
    QAbstractButton::enterEvent(event);
}

void ThemeToggle::leaveEvent(QEvent* event) {
    hovered_ = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void ThemeToggle::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    Fonts::preparePainter(&painter);
    const QRectF bounds = QRectF(rect()).adjusted(2.5, 2.5, -2.5, -2.5);
    if (hovered_ || isDown()) {
        QPainterPath path;
        path.addRoundedRect(bounds, 6, 6);
        painter.fillPath(path, Theme::hover());
    }
    const QString icon = Theme::mode() == Theme::Mode::Dark ? QStringLiteral("moon-02")
                                                           : QStringLiteral("sun-03");
    const QString iconPath = (hovered_ || isDown()) ? Icons::solid(icon) : Icons::stroke(icon);
    const QPixmap pix =
        Icons::pixmap(iconPath, 16, Theme::mutedForeground(), devicePixelRatioF());
    painter.drawPixmap((width() - 16) / 2, (height() - 16) / 2, pix);
}

}  // namespace edgeqt
