#include "window.hpp"
#include "glb_loader.hpp"
#include "hdri_loader.hpp"
#include "viewport.hpp"

#include <edgeqt/button.hpp>
#include <edgeqt/checkbox.hpp>
#include <edgeqt/dropdown.hpp>
#include <edgeqt/empty.hpp>
#include <edgeqt/fonts.hpp>
#include <edgeqt/icons.hpp>
#include <edgeqt/input.hpp>
#include <edgeqt/select.hpp>
#include <edgeqt/slider.hpp>
#include <edgeqt/theme.hpp>

#include <QAbstractAnimation>
#include <QAbstractButton>
#include <QColor>
#include <QColorDialog>
#include <QDir>
#include <QEasingCurve>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QList>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>

#include <functional>
#include <memory>
#include <tuple>

using edgeqt::Button;
using edgeqt::Fonts;
using edgeqt::Icons;
using edgeqt::Input;
using edgeqt::Label;
using edgeqt::Select;
using edgeqt::Separator;
using edgeqt::Slider;
using edgeqt::Switch;
using edgeqt::Theme;
using edgeqt::Tooltip;

namespace {

void paintPanel(QWidget* widget) {
    widget->setAttribute(Qt::WA_StyledBackground, true);
    Theme::bindStyle(widget, []() {
        return QStringLiteral("background: %1; color: %2;")
            .arg(Theme::css(Theme::card()), Theme::css(Theme::cardForeground()));
    });
}

QColor sheetSurface() {
    return Theme::mode() == Theme::Mode::Light ? Theme::popover() : Theme::background();
}

void paintSheetSurface(QWidget* widget) {
    if (widget == nullptr) {
        return;
    }
    widget->setAutoFillBackground(true);
    widget->setAttribute(Qt::WA_StyledBackground, true);
    Theme::bindStyle(widget, []() {
        return QStringLiteral("background: %1; color: %2; border: none;")
            .arg(Theme::css(sheetSurface()), Theme::css(Theme::foreground()));
    });
}

void bindThemedIcon(Button* button, const QString& path, int size = 16) {
    const auto apply = [button, path, size]() {
        button->setIcon(Icons::icon(path, Theme::mutedForeground()));
        button->setIconSize(QSize(size, size));
    };
    apply();
    QObject::connect(&Theme::instance(), &Theme::changed, button, apply);
}

void bindMutedIcon(Button* button, const QString& name, int size = 16) {
    bindThemedIcon(button, Icons::stroke(name), size);
}

Button* iconToolButton(const QString& iconPath, const QString& tip, QWidget* parent) {
    auto* button = new Button(parent);
    button->setVariant(Button::Variant::Ghost);
    button->setSize(Button::Size::IconSm);
    bindThemedIcon(button, iconPath, 14);
    Tooltip::install(button, tip, Tooltip::Side::Top);
    return button;
}

void bindListRowStyle(QPushButton* row) {
    Theme::bindStyle(row, []() {
        return QStringLiteral(
                   "QPushButton {"
                   "  text-align: left;"
                   "  padding: 0 10px;"
                   "  border: none;"
                   "  border-radius: 6px;"
                   "  background: transparent;"
                   "  color: %1;"
                   "}"
                   "QPushButton:hover {"
                   "  background: %2;"
                   "}"
                   "QPushButton:checked {"
                   "  background: %3;"
                   "  color: %4;"
                   "}")
            .arg(Theme::css(Theme::foreground()), Theme::css(Theme::hover()),
                 Theme::css(Theme::accent()), Theme::css(Theme::accentForeground()));
    });
}

void clearLayout(QLayout* layout) {
    if (layout == nullptr) {
        return;
    }
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (item->widget() != nullptr) {
            item->widget()->deleteLater();
        }
        delete item;
    }
}

QString uniqueDestPath(const QString& dir, const QString& fileName) {
    const QFileInfo info(fileName);
    QString dest = QDir(dir).filePath(info.fileName());
    int n = 2;
    while (QFile::exists(dest)) {
        dest = QDir(dir).filePath(
            QStringLiteral("%1-%2.%3").arg(info.completeBaseName(), QString::number(n++), info.suffix()));
    }
    return dest;
}

class HoverFilter : public QObject {
public:
    using Callback = std::function<void(bool)>;
    HoverFilter(QObject* parent, Callback cb) : QObject(parent), cb_(std::move(cb)) {}
protected:
    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::Enter) cb_(true);
        else if (event->type() == QEvent::Leave) cb_(false);
        return QObject::eventFilter(obj, event);
    }
private:
    Callback cb_;
};

class RailButton : public QAbstractButton {
public:
    RailButton(const QString& iconPath, const QString& tip, QWidget* parent)
        : QAbstractButton(parent), iconPath_(iconPath) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(36, 36);
        setAttribute(Qt::WA_Hover, true);
        Tooltip::install(this, tip, Tooltip::Side::Right);
        connect(&Theme::instance(), &Theme::changed, this, [this]() { update(); });
    }

    QSize sizeHint() const override { return {36, 36}; }

protected:
    void nextCheckState() override {}

    void enterEvent(QEnterEvent* event) override {
        QAbstractButton::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent* event) override {
        QAbstractButton::leaveEvent(event);
        update();
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        Fonts::preparePainter(&painter);
        const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(bounds, Theme::radiusMd(), Theme::radiusMd());
        if (isChecked()) {
            painter.fillPath(path, Theme::accent());
        } else if (underMouse()) {
            painter.fillPath(path, Theme::hover());
        }
        const QColor color = isChecked() ? Theme::accentForeground() : Theme::mutedForeground();
        const QPixmap pix = Icons::pixmap(iconPath_, 18, color, devicePixelRatioF());
        const QSize logical = pix.deviceIndependentSize().toSize();
        painter.drawPixmap((width() - logical.width()) / 2, (height() - logical.height()) / 2, pix);
    }

private:
    QString iconPath_;
};

class SettingsSheet : public QWidget {
public:
    std::function<void(bool, int)> onOpenChanged;

    SettingsSheet(QWidget* window, QWidget* anchor) : QWidget(window), anchor_(anchor) {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_OpaquePaintEvent, false);
        hide();

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(8, 6, 8, 8);
        root->setSpacing(4);

        auto* header = new QWidget(this);
        paintSheetSurface(header);
        headerLayout_ = new QHBoxLayout(header);
        headerLayout_->setContentsMargins(4, 0, 2, 0);
        headerLayout_->setSpacing(0);
        title_ = new QLabel(header);
        title_->setFont(Fonts::medium(10.5));
        title_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        Theme::bindStyle(title_, []() {
            return QStringLiteral("color: %1; background: transparent;")
                .arg(Theme::css(Theme::foreground()));
        });
        headerLayout_->addWidget(title_, 1, Qt::AlignVCenter);
        actionsHost_ = new QWidget(header);
        auto* actionsLay = new QHBoxLayout(actionsHost_);
        actionsLay->setContentsMargins(0, 0, 0, 0);
        actionsLay->setSpacing(0);
        headerLayout_->addWidget(actionsHost_, 0, Qt::AlignVCenter);
        auto* close = new Button(header);
        close->setVariant(Button::Variant::Ghost);
        close->setSize(Button::Size::IconSm);
        bindMutedIcon(close, QStringLiteral("close"), 14);
        connect(close, &Button::clicked, this, [this]() { closeSheet(); });
        headerLayout_->addWidget(close, 0, Qt::AlignVCenter);
        header->setMinimumHeight(28);
        root->addWidget(header);

        stack_ = new QStackedWidget(this);
        paintSheetSurface(stack_);
        root->addWidget(stack_, 1);

        auto applyFill = [this]() {
            QPalette pal = palette();
            pal.setColor(QPalette::Window, sheetSurface());
            pal.setColor(QPalette::Base, sheetSurface());
            pal.setColor(QPalette::Button, sheetSurface());
            setPalette(pal);
            if (stack_ != nullptr) {
                stack_->setPalette(pal);
            }
            update();
        };
        applyFill();
        connect(&Theme::instance(), &Theme::changed, this, applyFill);

        if (anchor_ != nullptr) {
            anchor_->installEventFilter(this);
        }
        if (window != nullptr) {
            window->installEventFilter(this);
        }
    }

    int addPage(const QString& title, QWidget* page) {
        titles_.push_back(title);
        return stack_->addWidget(page);
    }

    void setPageActions(int page, const QList<QWidget*>& actions) {
        if (actionsHost_ == nullptr || actions.isEmpty()) {
            return;
        }
        auto* bar = new QWidget(actionsHost_);
        auto* layout = new QHBoxLayout(bar);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        for (QWidget* action : actions) {
            if (action != nullptr) {
                action->setParent(bar);
                layout->addWidget(action, 0, Qt::AlignVCenter);
            }
        }
        bar->hide();
        actionsHost_->layout()->addWidget(bar);
        pageBars_.insert(page, bar);
        refreshPageActions();
    }

    void togglePage(int index) {
        if (open_ && stack_->currentIndex() == index) {
            closeSheet();
            return;
        }
        showPage(index);
    }

    void showPage(int index) {
        if (index < 0 || index >= stack_->count()) {
            return;
        }
        stack_->setCurrentIndex(index);
        title_->setText(titles_.value(index));
        refreshPageActions();
        if (open_) {
            relayout(true);
            notify();
            return;
        }
        open_ = true;
        show();
        raise();
        const QRect to = shownRect();
        QRect from = to;
        from.setWidth(0);
        setGeometry(from);
        animateTo(to, true);
        notify();
    }

    void closeSheet() {
        if (!open_) {
            return;
        }
        open_ = false;
        QRect to = geometry();
        to.setWidth(0);
        animateTo(to, false);
        notify();
        revealViewport();
    }

    bool isOpen() const { return open_; }
    int currentIndex() const { return stack_->currentIndex(); }

protected:
    void paintEvent(QPaintEvent*) override {
        if (width() < 12) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        Fonts::preparePainter(&painter);
        const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(bounds, Theme::radiusXl(), Theme::radiusXl());
        painter.fillPath(path, sheetSurface());
        painter.setPen(QPen(Theme::border(), 1.0));
        painter.drawPath(path);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move) {
            if (open_ && (anim_ == nullptr || anim_->state() != QAbstractAnimation::Running)) {
                relayout(true);
            }
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    static constexpr int kMinWidth = 240;
    static constexpr int kMaxWidth = 380;
    static constexpr int kDefaultWidth = 280;

    int dynamicWidth() const {
        if (anchor_ == nullptr) {
            return kDefaultWidth;
        }
        const int w = qRound(anchor_->width() * 0.25);
        return qBound(kMinWidth, w, kMaxWidth);
    }

    QRect shownRect() const {
        if (anchor_ == nullptr || parentWidget() == nullptr) {
            return QRect(0, 0, kDefaultWidth, 400);
        }
        const int w = dynamicWidth();
        const QPoint topLeft = anchor_->mapTo(parentWidget(), QPoint(8, 8));
        return QRect(topLeft, QSize(w, qMax(120, anchor_->height() - 16)));
    }

    void relayout(bool visible) {
        raise();
        setGeometry(visible ? shownRect() : QRect(shownRect().topLeft(), QSize(0, shownRect().height())));
    }

    void stopAnim() {
        if (anim_ != nullptr) {
            anim_->stop();
            anim_->deleteLater();
            anim_ = nullptr;
        }
    }

    void animateTo(const QRect& target, bool opening) {
        stopAnim();
        anim_ = new QPropertyAnimation(this, "geometry", this);
        anim_->setDuration(Theme::durationSheet());
        anim_->setEasingCurve(QEasingCurve::OutQuart);
        anim_->setStartValue(geometry());
        anim_->setEndValue(target);
        connect(anim_, &QPropertyAnimation::valueChanged, this, [this]() {
            if (!open_ && width() < 12) {
                hide();
            }
            revealViewport();
        });
        connect(anim_, &QPropertyAnimation::finished, this, [this, opening]() {
            anim_->deleteLater();
            anim_ = nullptr;
            if (!opening) {
                hide();
                move(-10000, -10000);
            } else {
                relayout(true);
            }
            revealViewport();
        });
        anim_->start();
    }

    void revealViewport() {
        if (anchor_ != nullptr) {
            anchor_->update();
        }
        if (parentWidget() != nullptr) {
            parentWidget()->update();
        }
    }

    void refreshPageActions() {
        const int current = stack_ != nullptr ? stack_->currentIndex() : -1;
        for (auto it = pageBars_.cbegin(); it != pageBars_.cend(); ++it) {
            if (it.value() != nullptr) {
                it.value()->setVisible(it.key() == current);
            }
        }
        if (actionsHost_ != nullptr) {
            actionsHost_->setVisible(pageBars_.contains(current));
        }
    }

    void notify() {
        if (onOpenChanged) {
            onOpenChanged(open_, open_ ? stack_->currentIndex() : -1);
        }
    }

    QWidget* anchor_ = nullptr;
    QLabel* title_ = nullptr;
    QHBoxLayout* headerLayout_ = nullptr;
    QWidget* actionsHost_ = nullptr;
    QHash<int, QWidget*> pageBars_;
    QStackedWidget* stack_ = nullptr;
    QStringList titles_;
    QPropertyAnimation* anim_ = nullptr;
    bool open_ = false;
};

QWidget* makeScrollPage(QVBoxLayout** layoutOut) {
    auto* inner = new QWidget;
    paintSheetSurface(inner);
    auto* layout = new QVBoxLayout(inner);
    layout->setContentsMargins(2, 2, 2, 6);
    layout->setSpacing(6);
    *layoutOut = layout;
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(inner);
    paintSheetSurface(scroll);
    paintSheetSurface(scroll->viewport());
    Theme::bindStyle(scroll, []() {
        const QString fill = Theme::css(sheetSurface());
        return QStringLiteral(
                   "QScrollArea { background: %1; border: none; }"
                   "QScrollArea > QWidget { background: %1; }")
            .arg(fill, fill);
    });
    return scroll;
}

QLabel* caption(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setFont(Fonts::medium(8.5));
    Theme::bindStyle(label, []() {
        return QStringLiteral("color: %1;").arg(Theme::css(Theme::mutedForeground()));
    });
    return label;
}

QLabel* valueLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setFont(Fonts::medium(8.5));
    Theme::bindStyle(label, []() {
        return QStringLiteral("color: %1;").arg(Theme::css(Theme::foreground()));
    });
    return label;
}

QWidget* sliderRow(const QString& title, Slider* slider, QLabel* value, QWidget* parent) {
    auto* wrap = new QWidget(parent);
    auto* layout = new QVBoxLayout(wrap);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    auto* header = new QWidget(wrap);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->addWidget(caption(title, header));
    headerLayout->addStretch(1);
    headerLayout->addWidget(value);
    layout->addWidget(header);
    layout->addWidget(slider);
    return wrap;
}

QWidget* selectRow(const QString& title, Select* select, QWidget* parent) {
    auto* wrap = new QWidget(parent);
    auto* layout = new QVBoxLayout(wrap);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(caption(title, wrap));
    layout->addWidget(select);
    return wrap;
}

QWidget* switchRow(const QString& title, Switch* control, QWidget* parent) {
    auto* wrap = new QWidget(parent);
    auto* layout = new QHBoxLayout(wrap);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new Label(title, wrap), 1);
    layout->addWidget(control, 0, Qt::AlignRight);
    return wrap;
}

QPushButton* swatch(const QColor& color, QWidget* parent) {
    auto* button = new QPushButton(parent);
    button->setFixedSize(72, 22);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    Theme::bindStyle(button, [color]() {
        return QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 4px; }")
            .arg(color.name(), Theme::css(Theme::border()));
    });
    return button;
}

QJsonArray loadJsonArray(const QString& key) {
    QSettings settings(QStringLiteral("glstudio"), QStringLiteral("glstudio"));
    const QJsonDocument doc = QJsonDocument::fromJson(settings.value(key).toByteArray());
    return doc.isArray() ? doc.array() : QJsonArray();
}

void saveJsonArray(const QString& key, const QJsonArray& array) {
    QSettings settings(QStringLiteral("glstudio"), QStringLiteral("glstudio"));
    settings.setValue(key, QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QWidget* buttonRow(const QList<QWidget*>& buttons, QWidget* parent) {
    auto* wrap = new QWidget(parent);
    auto* layout = new QHBoxLayout(wrap);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    for (QWidget* button : buttons) {
        layout->addWidget(button, 1);
    }
    return wrap;
}

void bindSwatchStyle(QPushButton* button, const QColor& color) {
    Theme::bindStyle(button, [color]() {
        return QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 4px; }")
            .arg(color.name(), Theme::css(Theme::border()));
    });
}

class CollapsibleSection : public QWidget {
public:
    CollapsibleSection(const QString& title, bool collapsed, QWidget* parent)
        : QWidget(parent), title_(title), collapsed_(collapsed) {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        header_ = new QPushButton(this);
        header_->setFlat(true);
        header_->setCursor(Qt::PointingHandCursor);
        header_->setFocusPolicy(Qt::NoFocus);
        header_->setFont(Fonts::medium(8.5));
        header_->setFixedHeight(24);
        header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        updateHeader(title);
        Theme::bindStyle(header_, []() {
            return QStringLiteral(
                "QPushButton { text-align: left; padding: 0 2px; border: none; "
                "border-radius: 4px; background: transparent; color: %1; }"
                "QPushButton:hover { background: %2; }")
                .arg(Theme::css(Theme::mutedForeground()), Theme::css(Theme::hover()));
        });
        root->addWidget(header_);

        content_ = new QWidget(this);
        auto* contentLay = new QVBoxLayout(content_);
        contentLay->setContentsMargins(0, 4, 0, 4);
        contentLay->setSpacing(6);
        root->addWidget(content_);
        content_->setVisible(!collapsed_);

        connect(header_, &QPushButton::clicked, this, [this]() {
            if (collapsed_) {
                if (auto* p = parentWidget()) {
                    for (auto* child : p->children()) {
                        if (auto* sibling = dynamic_cast<CollapsibleSection*>(child)) {
                            if (sibling != this) sibling->setCollapsed(true);
                        }
                    }
                }
                setCollapsed(false);
            } else {
                setCollapsed(true);
            }
        });
    }

    QVBoxLayout* contentLayout() const {
        return static_cast<QVBoxLayout*>(content_->layout());
    }

    QWidget* contentWidget() const { return content_; }

    void setCollapsed(bool c) {
        if (collapsed_ == c) return;
        collapsed_ = c;
        content_->setVisible(!collapsed_);
        updateHeader(title_);
    }

private:
    void updateHeader(const QString& title) {
        header_->setText(QStringLiteral(" %1").arg(title));
        if (collapsed_) {
            header_->setIcon(Icons::icon(Icons::stroke(QStringLiteral("chevron-right")), Theme::mutedForeground()));
        } else {
            header_->setIcon(Icons::icon(Icons::stroke(QStringLiteral("chevron-down")), Theme::mutedForeground()));
        }
    }

    QString title_;
    QPushButton* header_ = nullptr;
    QWidget* content_ = nullptr;
    bool collapsed_ = false;
};

void copyUriSidecar(const QString& srcDir, const QString& destDir, const QString& uri) {
    if (uri.isEmpty() || uri.startsWith(QLatin1String("data:"))) {
        return;
    }
    const QString relative = QUrl::fromPercentEncoding(uri.toUtf8());
    const QString from = QDir(srcDir).filePath(relative);
    const QString to = QDir(destDir).filePath(relative);
    if (!QFile::exists(from) || QFile::exists(to)) {
        return;
    }
    QDir().mkpath(QFileInfo(to).absolutePath());
    QFile::copy(from, to);
}

bool copyModelWithSidecars(const QString& src, const QString& dest) {
    if (!QFile::copy(src, dest)) {
        return false;
    }
    if (!src.endsWith(QLatin1String(".gltf"), Qt::CaseInsensitive)) {
        return true;
    }
    QFile file(src);
    if (!file.open(QIODevice::ReadOnly)) {
        return true;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return true;
    }
    const QString srcDir = QFileInfo(src).absolutePath();
    const QString destDir = QFileInfo(dest).absolutePath();
    const QJsonObject root = doc.object();
    for (const QJsonValue& value : root.value(QStringLiteral("buffers")).toArray()) {
        copyUriSidecar(srcDir, destDir, value.toObject().value(QStringLiteral("uri")).toString());
    }
    for (const QJsonValue& value : root.value(QStringLiteral("images")).toArray()) {
        copyUriSidecar(srcDir, destDir, value.toObject().value(QStringLiteral("uri")).toString());
    }
    return true;
}

}  // namespace

GlStudioWindow::GlStudioWindow(QWidget* parent) : edgeqt::Window(parent) {
    setTitle(QStringLiteral("glstudio"));
    setMinimumSize(1100, 700);

    auto* root = new QVBoxLayout(contentWidget());
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* body = new QWidget(contentWidget());
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    auto* rail = new QWidget(body);
    rail->setFixedWidth(52);
    paintPanel(rail);
    auto* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(8, 10, 8, 10);
    railLayout->setSpacing(4);

    viewport_ = new ModelViewport(body);
    auto* sheet = new SettingsSheet(this, viewport_);

    auto makePage = []() {
        QVBoxLayout* layout = nullptr;
        auto* scroll = makeScrollPage(&layout);
        QWidget* host = static_cast<QScrollArea*>(scroll)->widget();
        return std::tuple<QWidget*, QWidget*, QVBoxLayout*>{scroll, host, layout};
    };

    const auto [modelPage, modelHost, modelLay] = makePage();
    const auto [lookPage, lookHost, lookLay] = makePage();
    const auto [inspectPage, inspectHost, inspectLay] = makePage();
    const auto [cameraPage, cameraHost, cameraLay] = makePage();
    QWidget* worldHost = lookHost;
    QVBoxLayout* worldLay = lookLay;
    QWidget* hdriHost = lookHost;
    QVBoxLayout* hdriLay = lookLay;
    QWidget* lightsHost = lookHost;
    QVBoxLayout* lightsLay = lookLay;
    QWidget* presetsHost = lookHost;
    QVBoxLayout* presetsLay = lookLay;
    QWidget* sceneHost = inspectHost;
    QVBoxLayout* sceneLay = inspectLay;
    QWidget* materialsHost = inspectHost;
    QVBoxLayout* materialsLay = inspectLay;
    QWidget* animHost = inspectHost;
    QVBoxLayout* animLay = inspectLay;
    const auto [graphicsPage, graphicsHost, graphicsLay] = makePage();
    QWidget* postHost = graphicsHost;
    QVBoxLayout* postLay = graphicsLay;
    const auto [exportPage, exportHost, exportLay] = makePage();
    const auto [debugPage, debugHost, debugLay] = makePage();

    auto* fovValue = valueLabel(QStringLiteral("40°"), cameraHost);
    auto* fov = new Slider(cameraHost);
    fov->setRange(18, 75);
    fov->setValue(40);
    auto* autoRotate = new Switch(cameraHost);
    auto* auto360 = new Switch(cameraHost);
    auto* floating = new Switch(cameraHost);
    auto* grid = new Switch(worldHost);
    auto* axes = new Switch(worldHost);
    auto* textures = new Switch(materialsHost);
    textures->setChecked(true);
    auto* reset = new Button(QStringLiteral("Frame object"), cameraHost);
    reset->setVariant(Button::Variant::Outline);
    reset->setSize(Button::Size::Sm);
    auto* isolate = new Button(QStringLiteral("Isolate"), sceneHost);
    isolate->setVariant(Button::Variant::Outline);
    isolate->setSize(Button::Size::Sm);
    auto* clearIso = new Button(QStringLiteral("Show all"), sceneHost);
    clearIso->setVariant(Button::Variant::Ghost);
    clearIso->setSize(Button::Size::Sm);
    auto* wire = new Switch(materialsHost);
    auto* lightsVis = new Switch(lightsHost);

    auto* cameraSelect = new Select(cameraHost);
    cameraSelect->setSize(Select::Size::Xs);
    cameraSelect->setPlaceholder(QStringLiteral("Scene view"));
    auto* addCamera = new Button(QStringLiteral("Add"), cameraHost);
    addCamera->setVariant(Button::Variant::Outline);
    addCamera->setSize(Button::Size::Sm);
    auto* updateCamera = new Button(QStringLiteral("Update"), cameraHost);
    updateCamera->setVariant(Button::Variant::Outline);
    updateCamera->setSize(Button::Size::Sm);
    auto* deleteCamera = new Button(QStringLiteral("Delete"), cameraHost);
    deleteCamera->setVariant(Button::Variant::Ghost);
    deleteCamera->setSize(Button::Size::Sm);
    auto* sceneCameras = new Select(cameraHost);
    sceneCameras->setSize(Select::Size::Xs);
    sceneCameras->addItem(QStringLiteral("Orbit"), QStringLiteral("orbit"));
    auto* dofOn = new Switch(cameraHost);
    auto* dofValue = valueLabel(QStringLiteral("0.35"), cameraHost);
    auto* dof = new Slider(cameraHost);
    dof->setRange(0, 150);
    dof->setValue(35);
    auto* focusValue = valueLabel(QStringLiteral("7.0"), cameraHost);
    auto* focus = new Slider(cameraHost);
    focus->setRange(20, 4000);
    focus->setValue(700);
    auto* focusHint = caption(QStringLiteral("Alt-click the model to set focus"), cameraHost);
    focusHint->setWordWrap(true);
    auto* cameraName = new Input(cameraHost);
    cameraName->setPlaceholderText(QStringLiteral("Camera name"));
    
    auto* camSection = new CollapsibleSection(QStringLiteral("Camera"), true, cameraHost);
    auto* camL = camSection->contentLayout();
    camL->addWidget(sliderRow(QStringLiteral("Focal length"), fov, fovValue, camSection->contentWidget()));
    camL->addWidget(switchRow(QStringLiteral("Auto orbit"), autoRotate, camSection->contentWidget()));
    camL->addWidget(switchRow(QStringLiteral("Auto 360"), auto360, camSection->contentWidget()));
    camL->addWidget(switchRow(QStringLiteral("Floating"), floating, camSection->contentWidget()));
    camL->addWidget(reset);
    camL->addWidget(selectRow(QStringLiteral("Scene camera"), sceneCameras, camSection->contentWidget()));
    camL->addWidget(switchRow(QStringLiteral("Depth of field"), dofOn, camSection->contentWidget()));
    camL->addWidget(sliderRow(QStringLiteral("DOF amount"), dof, dofValue, camSection->contentWidget()));
    camL->addWidget(sliderRow(QStringLiteral("Focus distance"), focus, focusValue, camSection->contentWidget()));
    camL->addWidget(focusHint);
    cameraLay->addWidget(camSection);

    auto* savedCamSection = new CollapsibleSection(QStringLiteral("Saved cameras"), false, cameraHost);
    auto* savedCamL = savedCamSection->contentLayout();
    savedCamL->addWidget(selectRow(QStringLiteral("Saved cameras"), cameraSelect, savedCamSection->contentWidget()));
    savedCamL->addWidget(cameraName);
    savedCamL->addWidget(buttonRow({addCamera, updateCamera, deleteCamera}, savedCamSection->contentWidget()));
    cameraLay->addWidget(savedCamSection);
    cameraLay->addStretch(1);

    auto* exportSize = new Select(exportHost);
    exportSize->setSize(Select::Size::Xs);
    exportSize->addItem(QStringLiteral("1920 × 1080"), QStringLiteral("1920x1080"));
    exportSize->addItem(QStringLiteral("2560 × 1440"), QStringLiteral("2560x1440"));
    exportSize->addItem(QStringLiteral("3840 × 2160"), QStringLiteral("3840x2160"));
    exportSize->addItem(QStringLiteral("7680 × 4320"), QStringLiteral("7680x4320"));
    exportSize->setCurrentIndex(2);
    auto* exportBtn = new Button(QStringLiteral("Export render"), exportHost);
    exportBtn->setSize(Button::Size::Sm);
    exportBtn->setMaximumWidth(QWIDGETSIZE_MAX);
    exportBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* transparent = new Switch(exportHost);
    auto* turntableBtn = new Button(QStringLiteral("Export turntable"), exportHost);
    turntableBtn->setVariant(Button::Variant::Outline);
    turntableBtn->setSize(Button::Size::Sm);
    turntableBtn->setMaximumWidth(QWIDGETSIZE_MAX);
    turntableBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* exportSection = new CollapsibleSection(QStringLiteral("Export options"), true, exportHost);
    auto* expL = exportSection->contentLayout();
    expL->addWidget(selectRow(QStringLiteral("Resolution"), exportSize, exportSection->contentWidget()));
    expL->addWidget(switchRow(QStringLiteral("Transparent PNG"), transparent, exportSection->contentWidget()));
    expL->addWidget(exportBtn);
    expL->addWidget(turntableBtn);
    auto* ttHint = caption(QStringLiteral("Turntable writes MP4 if ffmpeg is on PATH, otherwise a PNG sequence"), exportSection->contentWidget());
    ttHint->setWordWrap(true);
    expL->addWidget(ttHint);
    exportLay->addWidget(exportSection);
    exportLay->addStretch(1);

    auto buildFileList = [this](QVBoxLayout* lay, const QString& current, const auto& entries, auto setter) {
        clearLayout(lay);
        for (const auto& entry : entries) {
            auto* row = new QPushButton(entry.name, lay->parentWidget());
            row->setCursor(Qt::PointingHandCursor);
            row->setCheckable(true);
            row->setChecked(entry.path.compare(current, Qt::CaseInsensitive) == 0);
            row->setFlat(true);
            row->setFocusPolicy(Qt::NoFocus);
            row->setFont(Fonts::regular(9.5));
            row->setFixedHeight(32);
            row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            bindListRowStyle(row);
            const QString path = entry.path;
            QObject::connect(row, &QPushButton::clicked, lay->parentWidget(), [this, setter, path, row]() {
                row->setChecked(true);
                if (!path.isEmpty()) {
                    (viewport_->*setter)(path);
                }
            });
            lay->addWidget(row);
        }
    };

    auto* bg = swatch(viewport_->backgroundColor(), worldHost);
    auto* modelSection = new CollapsibleSection(QStringLiteral("Model library"), true, modelHost);
    auto* modelL = modelSection->contentLayout();
    auto* modelList = new QWidget(modelSection->contentWidget());
    auto* modelListLay = new QVBoxLayout(modelList);
    modelListLay->setContentsMargins(0, 0, 0, 0);
    modelListLay->setSpacing(2);
    auto fillModelList = [this, modelListLay, buildFileList]() {
        buildFileList(modelListLay, viewport_->modelPath(), listPreviewModels(), &ModelViewport::setModelPath);
    };
    fillModelList();
    auto* refreshModels =
        iconToolButton(Icons::blender(QStringLiteral("file_refresh")), QStringLiteral("Refresh models"), modelSection->contentWidget());
    auto* importModel =
        iconToolButton(Icons::blender(QStringLiteral("file_folder")), QStringLiteral("Import model"), modelSection->contentWidget());
    auto* modelsHint = caption(QStringLiteral("Click a model, import a .glb/.gltf, or drop a file on the viewport"), modelSection->contentWidget());
    modelsHint->setWordWrap(true);
    modelL->addWidget(modelList);
    modelL->addWidget(modelsHint);
    modelLay->addWidget(modelSection);
    modelLay->addStretch(1);

    auto* sceneSection = new CollapsibleSection(QStringLiteral("Scene"), true, sceneHost);
    auto* sceneList = new QWidget(sceneSection->contentWidget());
    auto* sceneListLay = new QVBoxLayout(sceneList);
    sceneListLay->setContentsMargins(0, 0, 0, 0);
    sceneListLay->setSpacing(0);
    auto* selectedLabel = caption(QStringLiteral("Click a mesh in the viewport or list"), sceneSection->contentWidget());
    selectedLabel->setWordWrap(true);
    sceneSection->contentLayout()->addWidget(sceneList);
    sceneSection->contentLayout()->addWidget(buttonRow({isolate, clearIso}, sceneSection->contentWidget()));
    sceneSection->contentLayout()->addWidget(selectedLabel);
    sceneLay->addWidget(sceneSection);

    auto* animSection = new CollapsibleSection(QStringLiteral("Animation"), false, animHost);
    auto* clipSelect = new Select(animSection->contentWidget());
    clipSelect->setSize(Select::Size::Xs);
    clipSelect->setPlaceholder(QStringLiteral("No animation"));
    auto* play = new Switch(animSection->contentWidget());
    auto* loop = new Switch(animSection->contentWidget());
    loop->setChecked(true);
    auto* timeValue = valueLabel(QStringLiteral("0.00s"), animSection->contentWidget());
    auto* time = new Slider(animSection->contentWidget());
    time->setRange(0, 1000);
    time->setValue(0);
    auto* variantSelect = new Select(animSection->contentWidget());
    variantSelect->setSize(Select::Size::Xs);
    variantSelect->setPlaceholder(QStringLiteral("No variants"));
    auto* morphHost = new QWidget(animSection->contentWidget());
    auto* morphLay = new QVBoxLayout(morphHost);
    morphLay->setContentsMargins(0, 0, 0, 0);
    morphLay->setSpacing(8);
    animSection->contentLayout()->addWidget(selectRow(QStringLiteral("Clip"), clipSelect, animSection->contentWidget()));
    animSection->contentLayout()->addWidget(switchRow(QStringLiteral("Play"), play, animSection->contentWidget()));
    animSection->contentLayout()->addWidget(switchRow(QStringLiteral("Loop"), loop, animSection->contentWidget()));
    animSection->contentLayout()->addWidget(sliderRow(QStringLiteral("Time"), time, timeValue, animSection->contentWidget()));
    animSection->contentLayout()->addWidget(selectRow(QStringLiteral("Variant"), variantSelect, animSection->contentWidget()));
    animSection->contentLayout()->addWidget(caption(QStringLiteral("Morphs"), animSection->contentWidget()));
    animSection->contentLayout()->addWidget(morphHost);
    animLay->addWidget(animSection);

    auto* hdriList = new QWidget(hdriHost);
    auto* hdriListLay = new QVBoxLayout(hdriList);
    hdriListLay->setContentsMargins(0, 0, 0, 0);
    hdriListLay->setSpacing(2);
    auto fillHdriList = [this, hdriListLay, buildFileList]() {
        buildFileList(hdriListLay, viewport_->hdriPath(), listHdriFiles(), &ModelViewport::setHdriPath);
    };
    fillHdriList();
    auto* refreshHdris =
        iconToolButton(Icons::blender(QStringLiteral("file_refresh")), QStringLiteral("Refresh HDRIs"), hdriHost);
    auto* importHdri =
        iconToolButton(Icons::blender(QStringLiteral("file_folder")), QStringLiteral("Import HDRI"), hdriHost);
    auto* showHdri = new Switch(hdriHost);
    auto* hdriHint = caption(QStringLiteral("Click an HDRI, import .exr/.hdr, or drop it on the viewport"), hdriHost);
    hdriHint->setWordWrap(true);
    auto* envSection = new CollapsibleSection(QStringLiteral("Environment"), true, hdriHost);
    auto* envL = envSection->contentLayout();
    envL->addWidget(switchRow(QStringLiteral("HDRI visible"), showHdri, envSection->contentWidget()));
    envL->addWidget(hdriList);
    envL->addWidget(hdriHint);
    hdriLay->addWidget(envSection);

    auto* studioSection = new CollapsibleSection(QStringLiteral("Studio"), false, worldHost);
    auto* studioL = studioSection->contentLayout();
    auto* floor = new Switch(studioSection->contentWidget());
    floor->setChecked(false);
    auto* clay = new Switch(studioSection->contentWidget());
    studioL->addWidget(switchRow(QStringLiteral("Show floor grid"), grid, studioSection->contentWidget()));
    studioL->addWidget(switchRow(QStringLiteral("Show axes"), axes, studioSection->contentWidget()));
    studioL->addWidget(switchRow(QStringLiteral("Shadow catcher"), floor, studioSection->contentWidget()));
    studioL->addWidget(switchRow(QStringLiteral("Clay mode"), clay, studioSection->contentWidget()));
    studioL->addWidget(caption(QStringLiteral("Viewport background"), studioSection->contentWidget()));
    studioL->addWidget(bg, 0, Qt::AlignLeft);
    worldLay->addWidget(studioSection);

    auto* keyOn = new Switch(lightsHost);
    keyOn->setChecked(true);
    auto* fillOn = new Switch(lightsHost);
    fillOn->setChecked(true);
    auto* rimOn = new Switch(lightsHost);
    rimOn->setChecked(true);
    auto* envOn = new Switch(lightsHost);
    envOn->setChecked(true);
    auto* keyIntValue = valueLabel(QStringLiteral("0.55"), lightsHost);
    auto* keyInt = new Slider(lightsHost);
    keyInt->setRange(0, 300);
    keyInt->setValue(55);
    auto* fillIntValue = valueLabel(QStringLiteral("0.16"), lightsHost);
    auto* fillInt = new Slider(lightsHost);
    fillInt->setRange(0, 300);
    fillInt->setValue(16);
    auto* rimIntValue = valueLabel(QStringLiteral("0.22"), lightsHost);
    auto* rimInt = new Slider(lightsHost);
    rimInt->setRange(0, 300);
    rimInt->setValue(22);
    auto* envIntValue = valueLabel(QStringLiteral("1.15"), lightsHost);
    auto* envInt = new Slider(lightsHost);
    envInt->setRange(0, 250);
    envInt->setValue(115);
    auto* envRotValue = valueLabel(QStringLiteral("0°"), lightsHost);
    auto* envRot = new Slider(lightsHost);
    envRot->setRange(0, 360);
    envRot->setValue(0);
    auto* keyYawValue = valueLabel(QStringLiteral("38°"), lightsHost);
    auto* keyYaw = new Slider(lightsHost);
    keyYaw->setRange(-180, 180);
    keyYaw->setValue(38);
    auto* keyPitchValue = valueLabel(QStringLiteral("55°"), lightsHost);
    auto* keyPitch = new Slider(lightsHost);
    keyPitch->setRange(-80, 80);
    keyPitch->setValue(55);
    auto* fillYawValue = valueLabel(QStringLiteral("-112°"), lightsHost);
    auto* fillYaw = new Slider(lightsHost);
    fillYaw->setRange(-180, 180);
    fillYaw->setValue(-112);
    auto* fillPitchValue = valueLabel(QStringLiteral("14°"), lightsHost);
    auto* fillPitch = new Slider(lightsHost);
    fillPitch->setRange(-80, 80);
    fillPitch->setValue(14);
    auto* rimYawValue = valueLabel(QStringLiteral("-173°"), lightsHost);
    auto* rimYaw = new Slider(lightsHost);
    rimYaw->setRange(-180, 180);
    rimYaw->setValue(-173);
    auto* rimPitchValue = valueLabel(QStringLiteral("19°"), lightsHost);
    auto* rimPitch = new Slider(lightsHost);
    rimPitch->setRange(-80, 80);
    rimPitch->setValue(19);
    auto* keySwatch = swatch(viewport_->keyColor(), lightsHost);
    auto* fillSwatch = swatch(viewport_->fillColor(), lightsHost);
    auto* rimSwatch = swatch(viewport_->rimColor(), lightsHost);
    auto* lightsSection = new CollapsibleSection(QStringLiteral("Studio lights"), false, lightsHost);
    auto* lightsL = lightsSection->contentLayout();
    auto* sceneLightsOn = new Switch(lightsSection->contentWidget());
    sceneLightsOn->setChecked(true);
    lightsL->addWidget(switchRow(QStringLiteral("Scene lights"), sceneLightsOn, lightsSection->contentWidget()));
    lightsL->addWidget(switchRow(QStringLiteral("Key"), keyOn, lightsSection->contentWidget()));
    lightsL->addWidget(switchRow(QStringLiteral("Fill"), fillOn, lightsSection->contentWidget()));
    lightsL->addWidget(switchRow(QStringLiteral("Rim"), rimOn, lightsSection->contentWidget()));
    lightsL->addWidget(switchRow(QStringLiteral("Environment IBL"), envOn, lightsSection->contentWidget()));
    lightsL->addWidget(switchRow(QStringLiteral("Show lights"), lightsVis, lightsSection->contentWidget()));
    lightsL->addWidget(sliderRow(QStringLiteral("Key intensity"), keyInt, keyIntValue, lightsSection->contentWidget()));
    lightsL->addWidget(sliderRow(QStringLiteral("Fill intensity"), fillInt, fillIntValue, lightsSection->contentWidget()));
    lightsL->addWidget(sliderRow(QStringLiteral("Rim intensity"), rimInt, rimIntValue, lightsSection->contentWidget()));
    lightsL->addWidget(sliderRow(QStringLiteral("IBL intensity"), envInt, envIntValue, lightsSection->contentWidget()));
    lightsL->addWidget(sliderRow(QStringLiteral("IBL rotation"), envRot, envRotValue, lightsSection->contentWidget()));
    lightsL->addWidget(sliderRow(QStringLiteral("Key yaw"), keyYaw, keyYawValue, lightsSection->contentWidget()));
    lightsL->addWidget(sliderRow(QStringLiteral("Key pitch"), keyPitch, keyPitchValue, lightsSection->contentWidget()));
    lightsL->addWidget(sliderRow(QStringLiteral("Fill yaw"), fillYaw, fillYawValue, lightsSection->contentWidget()));
    lightsL->addWidget(sliderRow(QStringLiteral("Fill pitch"), fillPitch, fillPitchValue, lightsSection->contentWidget()));
    lightsL->addWidget(sliderRow(QStringLiteral("Rim yaw"), rimYaw, rimYawValue, lightsSection->contentWidget()));
    lightsL->addWidget(sliderRow(QStringLiteral("Rim pitch"), rimPitch, rimPitchValue, lightsSection->contentWidget()));
    lightsL->addWidget(caption(QStringLiteral("Key / Fill / Rim color"), lightsSection->contentWidget()));
    auto* colorRow = new QWidget(lightsSection->contentWidget());
    auto* colorLayout = new QHBoxLayout(colorRow);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    colorLayout->addWidget(keySwatch);
    colorLayout->addWidget(fillSwatch);
    colorLayout->addWidget(rimSwatch);
    colorLayout->addStretch(1);
    lightsL->addWidget(colorRow);
    lightsLay->addWidget(lightsSection);

    auto* presetSelect = new Select(presetsHost);
    presetSelect->setSize(Select::Size::Xs);
    presetSelect->setPlaceholder(QStringLiteral("No preset"));
    auto* addPreset = new Button(QStringLiteral("Add"), presetsHost);
    addPreset->setVariant(Button::Variant::Outline);
    addPreset->setSize(Button::Size::Sm);
    auto* updatePreset = new Button(QStringLiteral("Update"), presetsHost);
    updatePreset->setVariant(Button::Variant::Outline);
    updatePreset->setSize(Button::Size::Sm);
    auto* deletePreset = new Button(QStringLiteral("Delete"), presetsHost);
    deletePreset->setVariant(Button::Variant::Ghost);
    deletePreset->setSize(Button::Size::Sm);
    auto* presetHint = caption(QStringLiteral("Stores lighting, shading, post and GPU settings"), presetsHost);
    presetHint->setWordWrap(true);
    auto* presetName = new Input(presetsHost);
    presetName->setPlaceholderText(QStringLiteral("Preset name"));
    auto* lookSelect = new Select(presetsHost);
    lookSelect->setSize(Select::Size::Xs);
    lookSelect->addItem(QStringLiteral("Showroom"), QStringLiteral("0"));
    lookSelect->addItem(QStringLiteral("Soft daylight"), QStringLiteral("1"));
    lookSelect->addItem(QStringLiteral("Overcast"), QStringLiteral("2"));
    lookSelect->addItem(QStringLiteral("Night"), QStringLiteral("3"));
    lookSelect->addItem(QStringLiteral("Clay studio"), QStringLiteral("4"));
    auto* looksSection = new CollapsibleSection(QStringLiteral("Looks"), false, presetsHost);
    auto* looksL = looksSection->contentLayout();
    looksL->addWidget(selectRow(QStringLiteral("Look"), lookSelect, looksSection->contentWidget()));
    looksL->addWidget(selectRow(QStringLiteral("Lookdev preset"), presetSelect, looksSection->contentWidget()));
    looksL->addWidget(presetName);
    looksL->addWidget(buttonRow({addPreset, updatePreset, deletePreset}, looksSection->contentWidget()));
    looksL->addWidget(presetHint);
    presetsLay->addWidget(looksSection);
    presetsLay->addStretch(1);

    auto* gpu = new QLabel(QStringLiteral("Detecting GPU…"), graphicsHost);
    gpu->setWordWrap(true);
    gpu->setFont(Fonts::regular(8.5));
    Theme::bindStyle(gpu, []() {
        return QStringLiteral("color: %1;").arg(Theme::css(Theme::mutedForeground()));
    });
    auto* quality = new Select(graphicsHost);
    quality->setSize(Select::Size::Xs);
    quality->addItem(QStringLiteral("Low"), QStringLiteral("low"));
    quality->addItem(QStringLiteral("Medium"), QStringLiteral("medium"));
    quality->addItem(QStringLiteral("High"), QStringLiteral("high"));
    quality->addItem(QStringLiteral("Ultra"), QStringLiteral("ultra"));
    quality->addItem(QStringLiteral("Extreme"), QStringLiteral("extreme"));
    quality->setCurrentIndex(3);
    auto* aa = new Select(graphicsHost);
    aa->setSize(Select::Size::Xs);
    aa->addItem(QStringLiteral("Off"), QStringLiteral("0"));
    aa->addItem(QStringLiteral("2x MSAA"), QStringLiteral("2"));
    aa->addItem(QStringLiteral("4x MSAA"), QStringLiteral("4"));
    aa->addItem(QStringLiteral("8x MSAA"), QStringLiteral("8"));
    aa->addItem(QStringLiteral("16x MSAA"), QStringLiteral("16"));
    aa->setCurrentIndex(3);
    auto* shadowMap = new Select(graphicsHost);
    shadowMap->setSize(Select::Size::Xs);
    shadowMap->addItem(QStringLiteral("1024"), QStringLiteral("1024"));
    shadowMap->addItem(QStringLiteral("2048"), QStringLiteral("2048"));
    shadowMap->addItem(QStringLiteral("4096"), QStringLiteral("4096"));
    shadowMap->addItem(QStringLiteral("8192"), QStringLiteral("8192"));
    shadowMap->setCurrentIndex(2);
    auto* iblRes = new Select(graphicsHost);
    iblRes->setSize(Select::Size::Xs);
    iblRes->addItem(QStringLiteral("128"), QStringLiteral("128"));
    iblRes->addItem(QStringLiteral("256"), QStringLiteral("256"));
    iblRes->addItem(QStringLiteral("512"), QStringLiteral("512"));
    iblRes->addItem(QStringLiteral("1024"), QStringLiteral("1024"));
    iblRes->setCurrentIndex(2);
    auto* aniso = new Select(graphicsHost);
    aniso->setSize(Select::Size::Xs);
    aniso->addItem(QStringLiteral("1x"), QStringLiteral("1"));
    aniso->addItem(QStringLiteral("4x"), QStringLiteral("4"));
    aniso->addItem(QStringLiteral("8x"), QStringLiteral("8"));
    aniso->addItem(QStringLiteral("16x"), QStringLiteral("16"));
    aniso->setCurrentIndex(3);
    auto* scaleValue = valueLabel(QStringLiteral("135%"), graphicsHost);
    auto* scale = new Slider(graphicsHost);
    scale->setRange(50, 300);
    scale->setValue(135);
    auto* bloomPassValue = valueLabel(QStringLiteral("2"), graphicsHost);
    auto* bloomPass = new Slider(graphicsHost);
    bloomPass->setRange(0, 6);
    bloomPass->setValue(2);
    auto* vsync = new Switch(graphicsHost);
    vsync->setChecked(true);
    auto* shadowsOn = new Switch(graphicsHost);
    shadowsOn->setChecked(true);
    auto* ssaoOn = new Switch(graphicsHost);
    ssaoOn->setChecked(true);
    auto* bloomOn = new Switch(graphicsHost);
    bloomOn->setChecked(true);
    auto* gpuSection = new CollapsibleSection(QStringLiteral("GPU rendering"), true, graphicsHost);
    auto* gpuL = gpuSection->contentLayout();
    gpuL->addWidget(gpu);
    gpuL->addWidget(selectRow(QStringLiteral("Render quality"), quality, gpuSection->contentWidget()));
    gpuL->addWidget(selectRow(QStringLiteral("Antialiasing"), aa, gpuSection->contentWidget()));
    gpuL->addWidget(selectRow(QStringLiteral("Shadow map"), shadowMap, gpuSection->contentWidget()));
    gpuL->addWidget(selectRow(QStringLiteral("IBL resolution"), iblRes, gpuSection->contentWidget()));
    gpuL->addWidget(selectRow(QStringLiteral("Anisotropic filter"), aniso, gpuSection->contentWidget()));
    gpuL->addWidget(sliderRow(QStringLiteral("Resolution scale"), scale, scaleValue, gpuSection->contentWidget()));
    gpuL->addWidget(sliderRow(QStringLiteral("Bloom passes"), bloomPass, bloomPassValue, gpuSection->contentWidget()));
    gpuL->addWidget(switchRow(QStringLiteral("VSync"), vsync, gpuSection->contentWidget()));
    gpuL->addWidget(switchRow(QStringLiteral("Shadows"), shadowsOn, gpuSection->contentWidget()));
    gpuL->addWidget(switchRow(QStringLiteral("SSAO"), ssaoOn, gpuSection->contentWidget()));
    gpuL->addWidget(switchRow(QStringLiteral("Bloom"), bloomOn, gpuSection->contentWidget()));
    graphicsLay->addWidget(gpuSection);

    auto* normalsOn = new Switch(materialsHost);
    normalsOn->setChecked(true);
    auto* nrmValue = valueLabel(QStringLiteral("1.00"), materialsHost);
    auto* nrm = new Slider(materialsHost);
    nrm->setRange(0, 200);
    nrm->setValue(100);
    auto* roughValue = valueLabel(QStringLiteral("1.00"), materialsHost);
    auto* rough = new Slider(materialsHost);
    rough->setRange(5, 250);
    rough->setValue(100);
    auto* metalValue = valueLabel(QStringLiteral("1.00"), materialsHost);
    auto* metal = new Slider(materialsHost);
    metal->setRange(0, 200);
    metal->setValue(100);
    auto* aoMulValue = valueLabel(QStringLiteral("1.00"), materialsHost);
    auto* aoMul = new Slider(materialsHost);
    aoMul->setRange(0, 200);
    aoMul->setValue(100);
    auto* coatValue = valueLabel(QStringLiteral("1.00"), materialsHost);
    auto* coat = new Slider(materialsHost);
    coat->setRange(0, 250);
    coat->setValue(100);
    auto* directValue = valueLabel(QStringLiteral("1.00"), materialsHost);
    auto* direct = new Slider(materialsHost);
    direct->setRange(0, 250);
    direct->setValue(100);
    auto* shadowStrValue = valueLabel(QStringLiteral("0.72"), materialsHost);
    auto* shadowStr = new Slider(materialsHost);
    shadowStr->setRange(0, 100);
    shadowStr->setValue(72);
    auto* shadowSoftValue = valueLabel(QStringLiteral("1.00"), materialsHost);
    auto* shadowSoft = new Slider(materialsHost);
    shadowSoft->setRange(15, 400);
    shadowSoft->setValue(100);
    auto* shadeSection = new CollapsibleSection(QStringLiteral("Shading"), false, materialsHost);
    auto* shadeL = shadeSection->contentLayout();
    shadeL->addWidget(switchRow(QStringLiteral("Textures"), textures, materialsHost));
    shadeL->addWidget(switchRow(QStringLiteral("Wireframe"), wire, materialsHost));
    shadeL->addWidget(switchRow(QStringLiteral("Normal maps"), normalsOn, materialsHost));
    shadeL->addWidget(sliderRow(QStringLiteral("Normal intensity"), nrm, nrmValue, materialsHost));
    shadeL->addWidget(sliderRow(QStringLiteral("Roughness"), rough, roughValue, materialsHost));
    shadeL->addWidget(sliderRow(QStringLiteral("Metallic"), metal, metalValue, materialsHost));
    shadeL->addWidget(sliderRow(QStringLiteral("Ambient occlusion"), aoMul, aoMulValue, materialsHost));
    shadeL->addWidget(sliderRow(QStringLiteral("Clearcoat"), coat, coatValue, materialsHost));
    shadeL->addWidget(sliderRow(QStringLiteral("Direct light"), direct, directValue, materialsHost));
    shadeL->addWidget(sliderRow(QStringLiteral("Shadow strength"), shadowStr, shadowStrValue, materialsHost));
    shadeL->addWidget(sliderRow(QStringLiteral("Shadow softness"), shadowSoft, shadowSoftValue, materialsHost));
    materialsLay->addWidget(shadeSection);

    auto* matSection = new CollapsibleSection(QStringLiteral("Selected material"), true, materialsHost);
    auto* matL = matSection->contentLayout();
    auto* inspectName = caption(QStringLiteral("Select a part to inspect"), materialsHost);
    inspectName->setWordWrap(true);
    auto* matColor = swatch(QColor(220, 220, 220), materialsHost);
    auto* selMetalValue = valueLabel(QStringLiteral("0.00"), materialsHost);
    auto* selMetal = new Slider(materialsHost);
    selMetal->setRange(0, 100);
    auto* selRoughValue = valueLabel(QStringLiteral("0.45"), materialsHost);
    auto* selRough = new Slider(materialsHost);
    selRough->setRange(4, 100);
    selRough->setValue(45);
    auto* selTransValue = valueLabel(QStringLiteral("0.00"), materialsHost);
    auto* selTrans = new Slider(materialsHost);
    selTrans->setRange(0, 100);
    auto* selSheenValue = valueLabel(QStringLiteral("0.00"), materialsHost);
    auto* selSheen = new Slider(materialsHost);
    selSheen->setRange(0, 100);
    auto* selEmitValue = valueLabel(QStringLiteral("1.00"), materialsHost);
    auto* selEmit = new Slider(materialsHost);
    selEmit->setRange(0, 400);
    selEmit->setValue(100);
    auto* selUnlit = new Switch(materialsHost);
    matL->addWidget(inspectName);
    matL->addWidget(matColor, 0, Qt::AlignLeft);
    matL->addWidget(sliderRow(QStringLiteral("Metallic"), selMetal, selMetalValue, materialsHost));
    matL->addWidget(sliderRow(QStringLiteral("Roughness"), selRough, selRoughValue, materialsHost));
    matL->addWidget(sliderRow(QStringLiteral("Transmission"), selTrans, selTransValue, materialsHost));
    matL->addWidget(sliderRow(QStringLiteral("Sheen"), selSheen, selSheenValue, materialsHost));
    matL->addWidget(sliderRow(QStringLiteral("Emissive"), selEmit, selEmitValue, materialsHost));
    matL->addWidget(switchRow(QStringLiteral("Unlit"), selUnlit, materialsHost));
    materialsLay->addWidget(matSection);
    materialsLay->addStretch(1);

    auto* tonemap = new Select(postHost);
    tonemap->setSize(Select::Size::Xs);
    tonemap->addItem(QStringLiteral("ACES"), QStringLiteral("aces"));
    tonemap->addItem(QStringLiteral("Reinhard"), QStringLiteral("reinhard"));
    tonemap->addItem(QStringLiteral("Filmic"), QStringLiteral("filmic"));
    tonemap->addItem(QStringLiteral("Linear"), QStringLiteral("linear"));
    auto* exposureValue = valueLabel(QStringLiteral("1.05"), postHost);
    auto* exposure = new Slider(postHost);
    exposure->setRange(20, 300);
    exposure->setValue(105);
    auto* bloomValue = valueLabel(QStringLiteral("0.16"), postHost);
    auto* bloom = new Slider(postHost);
    bloom->setRange(0, 150);
    bloom->setValue(16);
    auto* bloomThValue = valueLabel(QStringLiteral("1.25"), postHost);
    auto* bloomTh = new Slider(postHost);
    bloomTh->setRange(20, 300);
    bloomTh->setValue(125);
    auto* vigValue = valueLabel(QStringLiteral("0.08"), postHost);
    auto* vig = new Slider(postHost);
    vig->setRange(0, 100);
    vig->setValue(8);
    auto* ssaoValue = valueLabel(QStringLiteral("1.00"), postHost);
    auto* ssao = new Slider(postHost);
    ssao->setRange(0, 200);
    ssao->setValue(100);
    auto* contrastValue = valueLabel(QStringLiteral("1.00"), postHost);
    auto* contrast = new Slider(postHost);
    contrast->setRange(20, 200);
    contrast->setValue(100);
    auto* satValue = valueLabel(QStringLiteral("1.00"), postHost);
    auto* sat = new Slider(postHost);
    sat->setRange(0, 200);
    sat->setValue(100);
    auto* tempValue = valueLabel(QStringLiteral("0.00"), postHost);
    auto* temp = new Slider(postHost);
    temp->setRange(-100, 100);
    temp->setValue(0);
    auto* tintValue = valueLabel(QStringLiteral("0.00"), postHost);
    auto* tint = new Slider(postHost);
    tint->setRange(-100, 100);
    tint->setValue(0);
    auto* sharpValue = valueLabel(QStringLiteral("0.00"), postHost);
    auto* sharp = new Slider(postHost);
    sharp->setRange(0, 150);
    sharp->setValue(0);
    auto* grainValue = valueLabel(QStringLiteral("0.00"), postHost);
    auto* grain = new Slider(postHost);
    grain->setRange(0, 100);
    grain->setValue(0);
    auto* caValue = valueLabel(QStringLiteral("0.00"), postHost);
    auto* ca = new Slider(postHost);
    ca->setRange(0, 100);
    ca->setValue(0);
    auto* postSection = new CollapsibleSection(QStringLiteral("Post FX"), true, postHost);
    auto* postL = postSection->contentLayout();
    postL->addWidget(selectRow(QStringLiteral("Tone mapping"), tonemap, postSection->contentWidget()));
    postL->addWidget(sliderRow(QStringLiteral("Exposure"), exposure, exposureValue, postSection->contentWidget()));
    postL->addWidget(sliderRow(QStringLiteral("Bloom"), bloom, bloomValue, postSection->contentWidget()));
    postL->addWidget(sliderRow(QStringLiteral("Bloom threshold"), bloomTh, bloomThValue, postSection->contentWidget()));
    postL->addWidget(sliderRow(QStringLiteral("Vignette"), vig, vigValue, postSection->contentWidget()));
    postL->addWidget(sliderRow(QStringLiteral("SSAO"), ssao, ssaoValue, postSection->contentWidget()));
    postL->addWidget(sliderRow(QStringLiteral("Contrast"), contrast, contrastValue, postSection->contentWidget()));
    postL->addWidget(sliderRow(QStringLiteral("Saturation"), sat, satValue, postSection->contentWidget()));
    postL->addWidget(sliderRow(QStringLiteral("Temperature"), temp, tempValue, postSection->contentWidget()));
    postL->addWidget(sliderRow(QStringLiteral("Tint"), tint, tintValue, postSection->contentWidget()));
    postL->addWidget(sliderRow(QStringLiteral("Sharpen"), sharp, sharpValue, postSection->contentWidget()));
    postL->addWidget(sliderRow(QStringLiteral("Film grain"), grain, grainValue, postSection->contentWidget()));
    postL->addWidget(sliderRow(QStringLiteral("Chromatic aberration"), ca, caValue, postSection->contentWidget()));
    postLay->addWidget(postSection);
    postLay->addStretch(1);

    auto* debugView = new Select(debugHost);
    debugView->setSize(Select::Size::Xs);
    debugView->addItem(QStringLiteral("Lit"), QStringLiteral("0"));
    debugView->addItem(QStringLiteral("Albedo"), QStringLiteral("1"));
    debugView->addItem(QStringLiteral("World normal"), QStringLiteral("2"));
    debugView->addItem(QStringLiteral("Roughness"), QStringLiteral("3"));
    debugView->addItem(QStringLiteral("Metallic"), QStringLiteral("4"));
    debugView->addItem(QStringLiteral("Occlusion"), QStringLiteral("5"));
    debugView->addItem(QStringLiteral("Direct lighting"), QStringLiteral("6"));
    debugView->addItem(QStringLiteral("Specular IBL"), QStringLiteral("7"));
    debugView->addItem(QStringLiteral("Shadows"), QStringLiteral("8"));
    debugView->addItem(QStringLiteral("Emissive"), QStringLiteral("9"));
    auto* debugHint = new QLabel(
        QStringLiteral("Use Direct lighting / Shadows / Specular IBL to see where each light contributes."),
        debugHost);
    debugHint->setWordWrap(true);
    debugHint->setFont(Fonts::regular(8.5));
    Theme::bindStyle(debugHint, []() {
        return QStringLiteral("color: %1;").arg(Theme::css(Theme::mutedForeground()));
    });
    auto* dbgSection = new CollapsibleSection(QStringLiteral("Debug"), true, debugHost);
    auto* dbgL = dbgSection->contentLayout();
    dbgL->addWidget(selectRow(QStringLiteral("Buffer"), debugView, dbgSection->contentWidget()));
    dbgL->addWidget(debugHint);
    auto* extHint = caption(QStringLiteral("Unsupported glTF extensions appear in the status bar as ignored."), dbgSection->contentWidget());
    extHint->setWordWrap(true);
    dbgL->addWidget(extHint);
    debugLay->addWidget(dbgSection);
    debugLay->addStretch(1);

    const int pageModel = sheet->addPage(QStringLiteral("Model"), modelPage);
    const int pageLook = sheet->addPage(QStringLiteral("Look"), lookPage);
    const int pageInspect = sheet->addPage(QStringLiteral("Inspect"), inspectPage);
    const int pageCamera = sheet->addPage(QStringLiteral("Camera"), cameraPage);
    const int pageExport = sheet->addPage(QStringLiteral("Export"), exportPage);
    const int pageGraphics = sheet->addPage(QStringLiteral("Graphics"), graphicsPage);
    const int pageDebug = sheet->addPage(QStringLiteral("Debug"), debugPage);
    sheet->setPageActions(pageModel, {refreshModels, importModel});
    sheet->setPageActions(pageLook, {refreshHdris, importHdri});

    struct RailItem {
        QString icon;
        QString tip;
        int page;
    };
    const RailItem railItems[] = {
        {Icons::blender(QStringLiteral("mesh_cube")), QStringLiteral("Model"), pageModel},
        {Icons::blender(QStringLiteral("world")), QStringLiteral("Look"), pageLook},
        {Icons::blender(QStringLiteral("outliner")), QStringLiteral("Inspect"), pageInspect},
        {Icons::blender(QStringLiteral("camera_data")), QStringLiteral("Camera"), pageCamera},
        {Icons::blender(QStringLiteral("export")), QStringLiteral("Export"), pageExport},
        {Icons::blender(QStringLiteral("shading_rendered")), QStringLiteral("Graphics"), pageGraphics},
        {Icons::blender(QStringLiteral("console")), QStringLiteral("Debug"), pageDebug},
    };
    QVector<RailButton*> railButtons;
    for (const RailItem& item : railItems) {
        auto* button = new RailButton(item.icon, item.tip, rail);
        const int page = item.page;
        connect(button, &QAbstractButton::clicked, this, [sheet, page]() { sheet->togglePage(page); });
        railLayout->addWidget(button, 0, Qt::AlignHCenter);
        railButtons.push_back(button);
    }
    railLayout->addStretch(1);
    sheet->onOpenChanged = [railButtons](bool open, int index) {
        for (int i = 0; i < railButtons.size(); ++i) {
            railButtons[i]->setChecked(open && i == index);
        }
    };

    bodyLayout->addWidget(rail);
    bodyLayout->addWidget(new Separator(Qt::Vertical, body));
    bodyLayout->addWidget(viewport_, 1);
    root->addWidget(body, 1);
    root->addWidget(new Separator(Qt::Horizontal, contentWidget()));

    auto* footer = new QWidget(contentWidget());
    footer->setFixedHeight(32);
    paintPanel(footer);
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(12, 0, 12, 0);
    auto* status = new QLabel(viewport_->statusText(), footer);
    status->setFont(Fonts::regular(8.5));
    Theme::bindStyle(status, []() {
        return QStringLiteral("color: %1;").arg(Theme::css(Theme::mutedForeground()));
    });
    footerLayout->addWidget(status);
    footerLayout->addStretch(1);
    root->addWidget(footer);

    auto paintSwatch = [bg](const QColor& color) {
        bindSwatchStyle(bg, color);
    };

    auto savedCameras = std::make_shared<QJsonArray>(loadJsonArray(QStringLiteral("cameras")));
    auto savedPresets = std::make_shared<QJsonArray>(loadJsonArray(QStringLiteral("presets")));
    auto fillNamedSelect = [](Select* select, const QJsonArray& items) {
        const QSignalBlocker blocker(select);
        const int current = select->currentIndex();
        select->clearItems();
        for (const QJsonValue& value : items) {
            select->addItem(value.toObject().value(QStringLiteral("name")).toString());
        }
        if (current >= 0 && !items.isEmpty()) {
            select->setCurrentIndex(qBound(0, current, items.size() - 1));
        }
    };
    fillNamedSelect(cameraSelect, *savedCameras);
    fillNamedSelect(presetSelect, *savedPresets);

    auto addNamedState = [=](std::shared_ptr<QJsonArray> list, Select* select, Input* nameInput,
                             const QString& key, const QString& prefix, const QJsonObject& state) {
        QString name = nameInput != nullptr ? nameInput->text().trimmed() : QString();
        if (name.isEmpty()) {
            name = QStringLiteral("%1 %2").arg(prefix).arg(list->size() + 1);
        }
        QJsonObject item = state;
        item.insert(QStringLiteral("name"), name);
        list->append(item);
        saveJsonArray(key, *list);
        {
            const QSignalBlocker blocker(select);
            fillNamedSelect(select, *list);
            select->setCurrentIndex(list->size() - 1);
        }
        if (nameInput != nullptr) {
            nameInput->clear();
        }
    };
    auto updateNamedState = [=](std::shared_ptr<QJsonArray> list, Select* select, const QString& key,
                                const QJsonObject& state) {
        const int index = select->currentIndex();
        if (index < 0 || index >= list->size()) {
            return;
        }
        QJsonObject item = state;
        item.insert(QStringLiteral("name"), list->at(index).toObject().value(QStringLiteral("name")));
        list->replace(index, item);
        saveJsonArray(key, *list);
    };
    auto deleteNamedState = [=](std::shared_ptr<QJsonArray> list, Select* select, const QString& key) {
        const int index = select->currentIndex();
        if (index < 0 || index >= list->size()) {
            return;
        }
        list->removeAt(index);
        saveJsonArray(key, *list);
        fillNamedSelect(select, *list);
    };

    connect(fov, &Slider::valueChanged, this, [=](int value) {
        fovValue->setText(QStringLiteral("%1°").arg(value));
        viewport_->setFov(static_cast<float>(value));
    });
    connect(autoRotate, &Switch::toggled, viewport_, &ModelViewport::setAutoRotate);
    connect(auto360, &Switch::toggled, viewport_, &ModelViewport::setAuto360);
    connect(floating, &Switch::toggled, viewport_, [this](bool e) { viewport_->setFloating(e); });
    connect(grid, &Switch::toggled, viewport_, &ModelViewport::setGridVisible);
    connect(axes, &Switch::toggled, viewport_, &ModelViewport::setAxesVisible);
    connect(textures, &Switch::toggled, viewport_, &ModelViewport::setTexturesEnabled);
    connect(wire, &Switch::toggled, viewport_, &ModelViewport::setWireframe);
    connect(lightsVis, &Switch::toggled, viewport_, &ModelViewport::setShowLights);
    connect(reset, &Button::clicked, viewport_, &ModelViewport::resetCamera);
    connect(isolate, &Button::clicked, this, [=]() {
        if (viewport_->selectedNode() >= 0) {
            viewport_->setIsolatedNode(viewport_->selectedNode());
        } else if (viewport_->selectedPart() >= 0) {
            const auto items = viewport_->outlinerItems();
            viewport_->setIsolatedNode(viewport_->selectedNode());
        }
    });
    connect(clearIso, &Button::clicked, viewport_, &ModelViewport::clearIsolation);
    connect(floor, &Switch::toggled, viewport_, &ModelViewport::setFloorCatcher);
    connect(clay, &Switch::toggled, viewport_, &ModelViewport::setClayMode);
    connect(dofOn, &Switch::toggled, viewport_, &ModelViewport::setDofEnabled);
    connect(transparent, &Switch::toggled, viewport_, &ModelViewport::setExportTransparentBackground);
    connect(lookSelect, &Select::currentIndexChanged, viewport_, &ModelViewport::applyLook);
    connect(sceneCameras, &Select::currentIndexChanged, viewport_, &ModelViewport::setSceneCameraIndex);
    connect(play, &Switch::toggled, viewport_, &ModelViewport::setAnimationPlaying);
    connect(loop, &Switch::toggled, viewport_, &ModelViewport::setAnimationLoop);
    connect(clipSelect, &Select::currentIndexChanged, viewport_, &ModelViewport::setAnimationIndex);
    connect(variantSelect, &Select::currentIndexChanged, viewport_, &ModelViewport::setVariantIndex);
    connect(selUnlit, &Switch::toggled, viewport_, &ModelViewport::setSelectedUnlit);
    connect(addCamera, &Button::clicked, this, [=]() {
        addNamedState(savedCameras, cameraSelect, cameraName, QStringLiteral("cameras"),
                      QStringLiteral("Camera"), viewport_->cameraState());
    });
    connect(updateCamera, &Button::clicked, this, [=]() {
        updateNamedState(savedCameras, cameraSelect, QStringLiteral("cameras"), viewport_->cameraState());
    });
    connect(deleteCamera, &Button::clicked, this, [=]() {
        deleteNamedState(savedCameras, cameraSelect, QStringLiteral("cameras"));
    });
    connect(cameraSelect, &Select::currentIndexChanged, this, [=](int index) {
        if (index < 0 || index >= savedCameras->size()) {
            return;
        }
        viewport_->applyCameraState(savedCameras->at(index).toObject());
    });
    connect(addPreset, &Button::clicked, this, [=]() {
        addNamedState(savedPresets, presetSelect, presetName, QStringLiteral("presets"),
                      QStringLiteral("Preset"), viewport_->lookState());
    });
    connect(updatePreset, &Button::clicked, this, [=]() {
        updateNamedState(savedPresets, presetSelect, QStringLiteral("presets"), viewport_->lookState());
    });
    connect(deletePreset, &Button::clicked, this, [=]() {
        deleteNamedState(savedPresets, presetSelect, QStringLiteral("presets"));
    });
    connect(presetSelect, &Select::currentIndexChanged, this, [=](int index) {
        if (index < 0 || index >= savedPresets->size()) {
            return;
        }
        viewport_->applyLookState(savedPresets->at(index).toObject());
    });
    connect(exportBtn, &Button::clicked, this, [=]() {
        const QString value = exportSize->currentValue();
        const QStringList parts = value.split(QLatin1Char('x'));
        const int w = parts.value(0).toInt();
        const int h = parts.value(1).toInt();
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Export render"),
            QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
                .filePath(QStringLiteral("glstudio-%1x%2.png").arg(w).arg(h)),
            QStringLiteral("PNG (*.png)"));
        if (!path.isEmpty()) {
            viewport_->exportRender(path, w, h);
        }
    });
    connect(turntableBtn, &Button::clicked, this, [=]() {
        const QString value = exportSize->currentValue();
        const QStringList parts = value.split(QLatin1Char('x'));
        const int w = parts.value(0).toInt();
        const int h = parts.value(1).toInt();
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Export turntable"),
            QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                .filePath(QStringLiteral("model-turntable.mp4")),
            QStringLiteral("MP4 (*.mp4)"));
        if (!path.isEmpty()) {
            viewport_->exportTurntable(path, w, h, 120);
        }
    });
    connect(refreshModels, &Button::clicked, this, [=]() { fillModelList(); });
    connect(importModel, &Button::clicked, this, [=]() {
        const QString src = QFileDialog::getOpenFileName(
            this, QStringLiteral("Import model"), QString(),
            QStringLiteral("glTF models (*.glb *.gltf);;All files (*.*)"));
        if (src.isEmpty()) {
            return;
        }
        const QString dest = uniqueDestPath(ensureUserModelsDir(), QFileInfo(src).fileName());
        if (!copyModelWithSidecars(src, dest)) {
            return;
        }
        viewport_->setModelPath(dest);
        fillModelList();
    });
    connect(refreshHdris, &Button::clicked, this, [=]() { fillHdriList(); });
    connect(importHdri, &Button::clicked, this, [=]() {
        const QString src = QFileDialog::getOpenFileName(
            this, QStringLiteral("Import HDRI"), QString(),
            QStringLiteral("HDR images (*.exr *.hdr);;All files (*.*)"));
        if (src.isEmpty()) {
            return;
        }
        const QString dest = uniqueDestPath(ensureUserHdriDir(), QFileInfo(src).fileName());
        if (!QFile::copy(src, dest)) {
            return;
        }
        viewport_->setHdriPath(dest);
        fillHdriList();
    });
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        viewport_->setLighting(Theme::mode() == Theme::Mode::Light ? ModelViewport::Lighting::Light
                                                                  : ModelViewport::Lighting::Dark);
    });
    viewport_->setLighting(Theme::mode() == Theme::Mode::Light ? ModelViewport::Lighting::Light
                                                              : ModelViewport::Lighting::Dark);
    connect(showHdri, &Switch::toggled, viewport_, &ModelViewport::setSkyVisible);
    connect(keyOn, &Switch::toggled, viewport_, &ModelViewport::setKeyEnabled);
    connect(fillOn, &Switch::toggled, viewport_, &ModelViewport::setFillEnabled);
    connect(rimOn, &Switch::toggled, viewport_, &ModelViewport::setRimEnabled);
    connect(envOn, &Switch::toggled, viewport_, &ModelViewport::setEnvEnabled);
    connect(sceneLightsOn, &Switch::toggled, viewport_, &ModelViewport::setSceneLights);
    connect(shadowsOn, &Switch::toggled, viewport_, &ModelViewport::setShadowsEnabled);
    connect(ssaoOn, &Switch::toggled, viewport_, &ModelViewport::setSsaoEnabled);
    connect(bloomOn, &Switch::toggled, viewport_, &ModelViewport::setBloomEnabled);
    connect(normalsOn, &Switch::toggled, viewport_, &ModelViewport::setNormalMaps);
    auto bindFloat = [=](Slider* slider, QLabel* label, auto setter, float div, int decimals, const QString& suffix = {}) {
        connect(slider, &Slider::valueChanged, this, [=](int value) {
            const float v = value / div;
            QString text = QString::number(v, 'f', decimals);
            if (!suffix.isEmpty()) {
                text += suffix;
            }
            label->setText(text);
            (viewport_->*setter)(v);
        });
    };
    bindFloat(keyInt, keyIntValue, &ModelViewport::setKeyIntensity, 100.0f, 2);
    bindFloat(fillInt, fillIntValue, &ModelViewport::setFillIntensity, 100.0f, 2);
    bindFloat(rimInt, rimIntValue, &ModelViewport::setRimIntensity, 100.0f, 2);
    bindFloat(envInt, envIntValue, &ModelViewport::setEnvIntensity, 100.0f, 2);
    connect(envRot, &Slider::valueChanged, this, [=](int value) {
        envRotValue->setText(QStringLiteral("%1°").arg(value));
        viewport_->setEnvRotation(static_cast<float>(value));
    });
    connect(keyYaw, &Slider::valueChanged, this, [=](int value) {
        keyYawValue->setText(QStringLiteral("%1°").arg(value));
        viewport_->setKeyYaw(static_cast<float>(value));
    });
    connect(keyPitch, &Slider::valueChanged, this, [=](int value) {
        keyPitchValue->setText(QStringLiteral("%1°").arg(value));
        viewport_->setKeyPitch(static_cast<float>(value));
    });
    connect(fillYaw, &Slider::valueChanged, this, [=](int value) {
        fillYawValue->setText(QStringLiteral("%1°").arg(value));
        viewport_->setFillYaw(static_cast<float>(value));
    });
    connect(fillPitch, &Slider::valueChanged, this, [=](int value) {
        fillPitchValue->setText(QStringLiteral("%1°").arg(value));
        viewport_->setFillPitch(static_cast<float>(value));
    });
    connect(rimYaw, &Slider::valueChanged, this, [=](int value) {
        rimYawValue->setText(QStringLiteral("%1°").arg(value));
        viewport_->setRimYaw(static_cast<float>(value));
    });
    connect(rimPitch, &Slider::valueChanged, this, [=](int value) {
        rimPitchValue->setText(QStringLiteral("%1°").arg(value));
        viewport_->setRimPitch(static_cast<float>(value));
    });
    auto bindColor = [=](QPushButton* button, auto getter, auto setter) {
        connect(button, &QPushButton::clicked, this, [=]() {
            const QColor next = QColorDialog::getColor((viewport_->*getter)(), this, QStringLiteral("Light color"));
            if (next.isValid()) {
                (viewport_->*setter)(next);
                bindSwatchStyle(button, next);
            }
        });
    };
    bindColor(keySwatch, &ModelViewport::keyColor, &ModelViewport::setKeyColor);
    bindColor(fillSwatch, &ModelViewport::fillColor, &ModelViewport::setFillColor);
    bindColor(rimSwatch, &ModelViewport::rimColor, &ModelViewport::setRimColor);
    connect(bg, &QPushButton::clicked, this, [=]() {
        const QColor next =
            QColorDialog::getColor(viewport_->backgroundColor(), this, QStringLiteral("Background"));
        if (next.isValid()) {
            viewport_->setBackgroundColor(next);
            paintSwatch(next);
        }
    });
    connect(quality, &Select::currentIndexChanged, this, [=](int index) {
        ModelViewport::Quality q = ModelViewport::Quality::High;
        if (index == 0) {
            q = ModelViewport::Quality::Low;
        } else if (index == 1) {
            q = ModelViewport::Quality::Medium;
        } else if (index == 3) {
            q = ModelViewport::Quality::Ultra;
        } else if (index == 4) {
            q = ModelViewport::Quality::Extreme;
        }
        viewport_->setQuality(q);
    });
    connect(aa, &Select::currentIndexChanged, this, [=](int index) {
        const int samples[] = {0, 2, 4, 8, 16};
        viewport_->setMsaaSamples(samples[qBound(0, index, 4)]);
    });
    connect(shadowMap, &Select::currentIndexChanged, this, [=]() {
        viewport_->setShadowSize(shadowMap->currentValue().toInt());
    });
    connect(iblRes, &Select::currentIndexChanged, this, [=]() {
        viewport_->setIblSize(iblRes->currentValue().toInt());
    });
    connect(aniso, &Select::currentIndexChanged, this, [=]() {
        viewport_->setAnisotropy(aniso->currentValue().toFloat());
    });
    connect(scale, &Slider::valueChanged, this, [=](int value) {
        scaleValue->setText(QStringLiteral("%1%").arg(value));
        viewport_->setRenderScale(value / 100.0f);
    });
    connect(vsync, &Switch::toggled, viewport_, &ModelViewport::setVSync);
    bindFloat(exposure, exposureValue, &ModelViewport::setExposure, 100.0f, 2);
    bindFloat(bloom, bloomValue, &ModelViewport::setBloom, 100.0f, 2);
    bindFloat(bloomTh, bloomThValue, &ModelViewport::setBloomThreshold, 100.0f, 2);
    bindFloat(vig, vigValue, &ModelViewport::setVignette, 100.0f, 2);
    bindFloat(ssao, ssaoValue, &ModelViewport::setSsaoIntensity, 100.0f, 2);
    bindFloat(contrast, contrastValue, &ModelViewport::setContrast, 100.0f, 2);
    bindFloat(sat, satValue, &ModelViewport::setSaturation, 100.0f, 2);
    bindFloat(temp, tempValue, &ModelViewport::setTemperature, 100.0f, 2);
    bindFloat(tint, tintValue, &ModelViewport::setTint, 100.0f, 2);
    bindFloat(sharp, sharpValue, &ModelViewport::setSharpen, 100.0f, 2);
    bindFloat(grain, grainValue, &ModelViewport::setGrain, 100.0f, 2);
    bindFloat(ca, caValue, &ModelViewport::setChromatic, 100.0f, 2);
    bindFloat(nrm, nrmValue, &ModelViewport::setNormalScale, 100.0f, 2);
    bindFloat(rough, roughValue, &ModelViewport::setRoughnessMul, 100.0f, 2);
    bindFloat(metal, metalValue, &ModelViewport::setMetallicMul, 100.0f, 2);
    bindFloat(aoMul, aoMulValue, &ModelViewport::setAoMul, 100.0f, 2);
    bindFloat(coat, coatValue, &ModelViewport::setClearcoatMul, 100.0f, 2);
    bindFloat(direct, directValue, &ModelViewport::setDirectMul, 100.0f, 2);
    bindFloat(shadowStr, shadowStrValue, &ModelViewport::setShadowStrength, 100.0f, 2);
    bindFloat(shadowSoft, shadowSoftValue, &ModelViewport::setShadowSoftness, 100.0f, 2);
    bindFloat(dof, dofValue, &ModelViewport::setDofAmount, 100.0f, 2);
    connect(focus, &Slider::valueChanged, this, [=](int value) {
        const float v = value / 100.0f;
        focusValue->setText(QString::number(v, 'f', 1));
        viewport_->setFocusDistance(v);
    });
    connect(time, &Slider::valueChanged, this, [=](int value) {
        const float duration = viewport_->animationDuration();
        const float t = duration * value / 1000.0f;
        timeValue->setText(QStringLiteral("%1s").arg(t, 0, 'f', 2));
        viewport_->setAnimationTime(t);
    });
    connect(selMetal, &Slider::valueChanged, this, [=](int value) {
        selMetalValue->setText(QString::number(value / 100.0, 'f', 2));
        viewport_->setSelectedMetallic(value / 100.0f);
    });
    connect(selRough, &Slider::valueChanged, this, [=](int value) {
        selRoughValue->setText(QString::number(value / 100.0, 'f', 2));
        viewport_->setSelectedRoughness(value / 100.0f);
    });
    connect(selTrans, &Slider::valueChanged, this, [=](int value) {
        selTransValue->setText(QString::number(value / 100.0, 'f', 2));
        viewport_->setSelectedTransmission(value / 100.0f);
    });
    connect(selSheen, &Slider::valueChanged, this, [=](int value) {
        selSheenValue->setText(QString::number(value / 100.0, 'f', 2));
        viewport_->setSelectedSheen(value / 100.0f);
    });
    connect(selEmit, &Slider::valueChanged, this, [=](int value) {
        selEmitValue->setText(QString::number(value / 100.0, 'f', 2));
        viewport_->setSelectedEmissiveGain(value / 100.0f);
    });
    connect(matColor, &QPushButton::clicked, this, [=]() {
        const PbrMaterial mat = viewport_->selectedMaterialData();
        const QColor current(qRound(mat.baseColor.x() * 255), qRound(mat.baseColor.y() * 255),
                             qRound(mat.baseColor.z() * 255));
        const QColor next = QColorDialog::getColor(current, this, QStringLiteral("Material color"));
        if (next.isValid()) {
            viewport_->setSelectedBaseColor(next);
            Theme::bindStyle(matColor, [next]() {
                return QStringLiteral("QPushButton { background: %1; border: 1px solid %2; border-radius: 4px; }")
                    .arg(next.name(), Theme::css(Theme::border()));
            });
        }
    });
    connect(bloomPass, &Slider::valueChanged, this, [=](int value) {
        bloomPassValue->setText(QString::number(value));
        viewport_->setBloomPasses(value);
    });
    connect(tonemap, &Select::currentIndexChanged, this, [=](int index) {
        viewport_->setTonemap(static_cast<ModelViewport::Tonemap>(qBound(0, index, 3)));
    });
    connect(debugView, &Select::currentIndexChanged, this, [=](int index) {
        viewport_->setDebugView(static_cast<ModelViewport::DebugView>(qBound(0, index, 9)));
    });
    connect(viewport_, &ModelViewport::cameraChanged, this, [=]() {
        const QSignalBlocker blocker(fov);
        fov->setValue(qRound(viewport_->fov()));
        fovValue->setText(QStringLiteral("%1°").arg(qRound(viewport_->fov())));
    });
    connect(viewport_, &ModelViewport::graphicsChanged, this, [=]() {
        auto blockSet = [](auto* c, auto v) {
            const QSignalBlocker b(c);
            c->setValue(v);
        };
        auto blockSetToggled = [](auto* c, bool v) {
            const QSignalBlocker b(c);
            c->setChecked(v);
        };
        auto blockSetIndex = [](auto* c, int v) {
            const QSignalBlocker b(c);
            c->setCurrentIndex(v);
        };

        switch (viewport_->quality()) {
            case ModelViewport::Quality::Low:
                blockSetIndex(quality, 0);
                break;
            case ModelViewport::Quality::Medium:
                blockSetIndex(quality, 1);
                break;
            case ModelViewport::Quality::High:
                blockSetIndex(quality, 2);
                break;
            case ModelViewport::Quality::Ultra:
                blockSetIndex(quality, 3);
                break;
            case ModelViewport::Quality::Extreme:
                blockSetIndex(quality, 4);
                break;
        }
        int aaIndex = 0;
        if (viewport_->msaaSamples() >= 16) {
            aaIndex = 4;
        } else if (viewport_->msaaSamples() >= 8) {
            aaIndex = 3;
        } else if (viewport_->msaaSamples() >= 4) {
            aaIndex = 2;
        } else if (viewport_->msaaSamples() >= 2) {
            aaIndex = 1;
        }
        blockSetIndex(aa, aaIndex);
        const int shadow = viewport_->shadowSize();
        blockSetIndex(shadowMap, shadow >= 8192 ? 3 : shadow >= 4096 ? 2 : shadow >= 2048 ? 1 : 0);
        const int ibl = viewport_->iblSize();
        blockSetIndex(iblRes, ibl >= 1024 ? 3 : ibl >= 512 ? 2 : ibl >= 256 ? 1 : 0);
        const float af = viewport_->anisotropy();
        blockSetIndex(aniso, af >= 15.5f ? 3 : af >= 7.5f ? 2 : af >= 3.5f ? 1 : 0);
        blockSet(bloomPass, viewport_->bloomPasses());
        bloomPassValue->setText(QString::number(viewport_->bloomPasses()));
        blockSetToggled(grid, viewport_->gridVisible());
        blockSetToggled(axes, viewport_->axesVisible());
        blockSetToggled(textures, viewport_->texturesEnabled());
        const int scalePct = qRound(viewport_->renderScale() * 100.0f);
        blockSet(scale, scalePct);
        scaleValue->setText(QStringLiteral("%1%").arg(scalePct));
        blockSetToggled(vsync, viewport_->vsync());
        blockSet(exposure, qRound(viewport_->exposure() * 100.0f));
        exposureValue->setText(QString::number(viewport_->exposure(), 'f', 2));
        blockSet(bloom, qRound(viewport_->bloom() * 100.0f));
        bloomValue->setText(QString::number(viewport_->bloom(), 'f', 2));
        blockSet(vig, qRound(viewport_->vignette() * 100.0f));
        vigValue->setText(QString::number(viewport_->vignette(), 'f', 2));
        blockSet(envInt, qRound(viewport_->envIntensity() * 100.0f));
        envIntValue->setText(QString::number(viewport_->envIntensity(), 'f', 2));
        blockSet(keyInt, qRound(viewport_->keyIntensity() * 100.0f));
        keyIntValue->setText(QString::number(viewport_->keyIntensity(), 'f', 2));
        blockSet(fillInt, qRound(viewport_->fillIntensity() * 100.0f));
        fillIntValue->setText(QString::number(viewport_->fillIntensity(), 'f', 2));
        blockSet(rimInt, qRound(viewport_->rimIntensity() * 100.0f));
        rimIntValue->setText(QString::number(viewport_->rimIntensity(), 'f', 2));
        blockSet(keyYaw, qRound(viewport_->keyYaw()));
        keyYawValue->setText(QStringLiteral("%1°").arg(qRound(viewport_->keyYaw())));
        blockSet(keyPitch, qRound(viewport_->keyPitch()));
        keyPitchValue->setText(QStringLiteral("%1°").arg(qRound(viewport_->keyPitch())));
        blockSet(fillYaw, qRound(viewport_->fillYaw()));
        fillYawValue->setText(QStringLiteral("%1°").arg(qRound(viewport_->fillYaw())));
        blockSet(fillPitch, qRound(viewport_->fillPitch()));
        fillPitchValue->setText(QStringLiteral("%1°").arg(qRound(viewport_->fillPitch())));
        blockSet(rimYaw, qRound(viewport_->rimYaw()));
        rimYawValue->setText(QStringLiteral("%1°").arg(qRound(viewport_->rimYaw())));
        blockSet(rimPitch, qRound(viewport_->rimPitch()));
        rimPitchValue->setText(QStringLiteral("%1°").arg(qRound(viewport_->rimPitch())));
        paintSwatch(viewport_->backgroundColor());
        bindSwatchStyle(keySwatch, viewport_->keyColor());
        bindSwatchStyle(fillSwatch, viewport_->fillColor());
        bindSwatchStyle(rimSwatch, viewport_->rimColor());
        if (!viewport_->gpuDetails().isEmpty()) {
            gpu->setText(viewport_->gpuDetails());
        } else if (!viewport_->gpuName().isEmpty()) {
            gpu->setText(viewport_->gpuName());
        }
        showHdri->setChecked(viewport_->skyVisible());
        floor->setChecked(viewport_->floorCatcher());
        autoRotate->setChecked(viewport_->autoRotate());
        auto360->setChecked(viewport_->auto360());
        floating->setChecked(viewport_->floating());
        clay->setChecked(viewport_->clayMode());
        fillModelList();
        fillHdriList();
        status->setText(viewport_->statusText());
    });

    auto fillOutliner = [=]() {
        clearLayout(sceneListLay);
        const auto items = viewport_->outlinerItems();
        for (const OutlinerItem& item : items) {
            auto* row = new QPushButton(QString(item.depth * 2, QLatin1Char(' ')) + item.name, sceneList);
            row->setCursor(Qt::PointingHandCursor);
            row->setCheckable(true);
            row->setChecked(item.part == viewport_->selectedPart() ||
                            (item.part < 0 && item.node == viewport_->selectedNode()));
            row->setFlat(true);
            row->setFocusPolicy(Qt::NoFocus);
            row->setFont(Fonts::regular(9.0));
            row->setFixedHeight(28);
            row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            bindListRowStyle(row);
            const int node = item.node;
            const int part = item.part;
            QObject::connect(row, &QPushButton::clicked, sceneList, [this, node, part]() {
                if (part >= 0) {
                    viewport_->setSelectedPart(part);
                } else {
                    viewport_->setSelectedNode(node);
                }
            });

            row->installEventFilter(new HoverFilter(row, [this, node, part](bool hovered) {
                if (hovered) {
                    if (part >= 0) viewport_->setHoveredPart(part);
                    else viewport_->setHoveredNode(node);
                } else {
                    if (part >= 0 && viewport_->hoveredPart() == part) viewport_->setHoveredPart(-1);
                    else if (viewport_->hoveredNode() == node) viewport_->setHoveredNode(-1);
                }
            }));

            sceneListLay->addWidget(row);
        }
    };
    auto fillClips = [=]() {
        const QSignalBlocker blocker(clipSelect);
        clipSelect->clearItems();
        const QStringList anims = viewport_->animationNames();
        for (const QString& name : anims) {
            clipSelect->addItem(name);
        }
        const bool hasAnims = !anims.isEmpty();
        clipSelect->setEnabled(hasAnims);
        play->setEnabled(hasAnims);
        loop->setEnabled(hasAnims);
        time->setEnabled(hasAnims);

        const QSignalBlocker vblock(variantSelect);
        variantSelect->clearItems();
        const QStringList variants = viewport_->variantNames();
        for (const QString& name : variants) {
            variantSelect->addItem(name);
        }
        variantSelect->setEnabled(!variants.isEmpty());

        const QSignalBlocker cblock(sceneCameras);
        sceneCameras->clearItems();
        const QStringList cams = viewport_->sceneCameraNames();
        int i = 0;
        for (const QString& name : cams) {
            sceneCameras->addItem(name, QString::number(i++));
        }
        sceneCameras->setCurrentIndex(0);
        sceneCameras->setEnabled(!cams.isEmpty());

        clearLayout(morphLay);
        const QStringList morphs = viewport_->morphNames();
        if (morphs.isEmpty()) {
            auto* lbl = caption(QStringLiteral("No morph targets"), morphHost);
            lbl->setEnabled(false);
            morphLay->addWidget(lbl);
        } else {
            const std::vector<float> weights = viewport_->morphWeights();
            for (int m = 0; m < morphs.size(); ++m) {
                auto* value = valueLabel(QString::number(m < static_cast<int>(weights.size()) ? weights[static_cast<size_t>(m)] : 0.0, 'f', 2), morphHost);
                auto* slider = new Slider(morphHost);
                slider->setRange(0, 100);
                slider->setValue(m < static_cast<int>(weights.size()) ? qRound(weights[static_cast<size_t>(m)] * 100.0f) : 0);
                morphLay->addWidget(sliderRow(morphs.at(m), slider, value, morphHost));
                connect(slider, &Slider::valueChanged, this, [=](int v) {
                    value->setText(QString::number(v / 100.0, 'f', 2));
                    viewport_->setMorphWeight(m, v / 100.0f);
                });
            }
        }
    };
    auto refreshInspector = [=]() {
        const PbrMaterial mat = viewport_->selectedMaterialData();
        const bool hasPart = viewport_->selectedPart() >= 0;
        const QString name = mat.name.isEmpty() ? QStringLiteral("Select a part to inspect") : mat.name;
        inspectName->setText(name);
        const QColor color(qRound(mat.baseColor.x() * 255), qRound(mat.baseColor.y() * 255),
                           qRound(mat.baseColor.z() * 255));
        bindSwatchStyle(matColor, color);
        matColor->setEnabled(hasPart);

        const QSignalBlocker b1(selMetal);
        const QSignalBlocker b2(selRough);
        const QSignalBlocker b3(selTrans);
        const QSignalBlocker b4(selSheen);
        const QSignalBlocker b5(selEmit);
        const QSignalBlocker b6(selUnlit);
        selMetal->setValue(qRound(mat.metallic * 100.0f));
        selRough->setValue(qRound(mat.roughness * 100.0f));
        selTrans->setValue(qRound(mat.transmission * 100.0f));
        selSheen->setValue(qRound(mat.sheenColor.x() * 100.0f));
        selEmit->setValue(qRound(mat.emissiveStrength * 100.0f));
        selUnlit->setChecked(mat.unlit);
        selMetalValue->setText(QString::number(mat.metallic, 'f', 2));
        selRoughValue->setText(QString::number(mat.roughness, 'f', 2));
        selTransValue->setText(QString::number(mat.transmission, 'f', 2));

        selMetal->setEnabled(hasPart);
        selRough->setEnabled(hasPart);
        selTrans->setEnabled(hasPart);
        selSheen->setEnabled(hasPart);
        selEmit->setEnabled(hasPart);
        selUnlit->setEnabled(hasPart);

        if (hasPart) {
            selectedLabel->setText(QStringLiteral("Part %1 · node %2")
                                       .arg(viewport_->selectedPart())
                                       .arg(viewport_->selectedNode()));
        } else {
            selectedLabel->setText(QStringLiteral("Click a mesh in the viewport or list"));
        }
    };
    connect(viewport_, &ModelViewport::modelChanged, this, [=]() {
        fillOutliner();
        fillClips();
        refreshInspector();
    });
    connect(viewport_, &ModelViewport::selectionChanged, this, [=]() {
        fillOutliner();
        refreshInspector();
    });
    connect(viewport_, &ModelViewport::animationChanged, this, [=]() {
        const QSignalBlocker blocker(time);
        const QSignalBlocker pblock(play);
        const float duration = viewport_->animationDuration();
        const int slider = duration > 0.0f ? qBound(0, qRound(viewport_->animationTime() / duration * 1000.0f), 1000) : 0;
        time->setValue(slider);
        timeValue->setText(QStringLiteral("%1s").arg(viewport_->animationTime(), 0, 'f', 2));
        play->setChecked(viewport_->animationPlaying());
    });
    fillOutliner();
    fillClips();
}
