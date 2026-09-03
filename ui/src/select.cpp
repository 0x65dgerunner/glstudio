#include <edgeqt/select.hpp>

#include <edgeqt/fonts.hpp>
#include <edgeqt/icons.hpp>
#include <edgeqt/theme.hpp>

#include <QFrame>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

namespace edgeqt {
namespace {

class SelectPopup : public QWidget {
public:
    SelectPopup(Select* owner, const QVector<Select::Item>& items, int current, const QSize& triggerSize)
        : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
          owner_(owner) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);

        const bool tiny = owner != nullptr && owner->size() == Select::Size::Xs;
        const int kRowHeight = tiny ? 28 : 32;
        constexpr int kMaxVisible = 8;
        constexpr int kSpacing = 2;
        constexpr int kPad = 4;

        auto* list = new QWidget(this);
        list->setAutoFillBackground(false);
        auto* listLayout = new QVBoxLayout(list);
        listLayout->setContentsMargins(kPad, kPad, kPad, kPad);
        listLayout->setSpacing(kSpacing);

        QWidget* currentRow = nullptr;
        for (int i = 0; i < items.size(); ++i) {
            auto* row = new QPushButton(items.at(i).text, list);
            row->setFlat(true);
            row->setCursor(Qt::PointingHandCursor);
            row->setCheckable(true);
            row->setChecked(i == current);
            row->setAutoFillBackground(false);
            row->setFocusPolicy(Qt::NoFocus);
            row->setFont(Fonts::regular(tiny ? 9.0 : 9.5));
            row->setFixedHeight(kRowHeight);
            row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
                           "QPushButton:hover, QPushButton:checked {"
                           "  background: %2;"
                           "}")
                    .arg(Theme::css(Theme::foreground()), Theme::css(Theme::hover()));
            });
            const int index = i;
            connect(row, &QPushButton::clicked, this, [this, index]() {
                if (owner_ != nullptr) {
                    owner_->setCurrentIndex(index);
                }
                close();
            });
            listLayout->addWidget(row);
            if (i == current) {
                currentRow = row;
            }
        }

        const int count = items.size();
        const int visible = qMin(count, kMaxVisible);
        const int contentH = count * kRowHeight + qMax(0, count - 1) * kSpacing + kPad * 2;
        const int viewH = visible * kRowHeight + qMax(0, visible - 1) * kSpacing + kPad * 2;
        list->setFixedHeight(contentH);
        list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        scroll_ = new QScrollArea(this);
        scroll_->setWidget(list);
        scroll_->setWidgetResizable(true);
        scroll_->setFrameShape(QFrame::NoFrame);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->setVerticalScrollBarPolicy(count > kMaxVisible ? Qt::ScrollBarAsNeeded
                                                                : Qt::ScrollBarAlwaysOff);
        scroll_->setAutoFillBackground(false);
        scroll_->viewport()->setAutoFillBackground(false);
        Theme::bindStyle(scroll_, []() {
            return QStringLiteral(
                "QScrollArea { background: transparent; border: none; }"
                "QScrollArea > QWidget { background: transparent; }");
        });

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(scroll_);

        setFixedWidth(qMax(triggerSize.width(), 160));
        setFixedHeight(viewH);
        currentRow_ = currentRow;
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        Fonts::preparePainter(&painter);
        const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(bounds, Theme::radiusMd(), Theme::radiusMd());
        painter.fillPath(path, Theme::popover());
        painter.setPen(QPen(Theme::border(), 1.0));
        painter.drawPath(path);
    }

    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
        if (scroll_ != nullptr && currentRow_ != nullptr) {
            scroll_->ensureWidgetVisible(currentRow_);
        }
    }

private:
    Select* owner_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* currentRow_ = nullptr;
};

}  // namespace

Select::Select(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFont(Fonts::regular(10.0));
    setAttribute(Qt::WA_Hover, true);
}

void Select::addItem(const QString& text, const QString& value) {
    items_.push_back({text, value.isEmpty() ? text : value});
    if (currentIndex_ < 0) {
        setCurrentIndex(0);
    }
    update();
}

void Select::setItems(const QStringList& items) {
    items_.clear();
    currentIndex_ = -1;
    for (const QString& item : items) {
        addItem(item);
    }
}

void Select::clearItems() {
    items_.clear();
    currentIndex_ = -1;
    update();
}

void Select::setPlaceholder(const QString& text) {
    placeholder_ = text;
    update();
}

void Select::setCurrentIndex(int index) {
    if (index < -1 || index >= items_.size()) {
        return;
    }
    if (currentIndex_ == index) {
        return;
    }
    currentIndex_ = index;
    update();
    emit currentIndexChanged(currentIndex_);
    emit currentTextChanged(currentText());
}

QString Select::currentText() const {
    if (currentIndex_ < 0 || currentIndex_ >= items_.size()) {
        return QString();
    }
    return items_.at(currentIndex_).text;
}

QString Select::currentValue() const {
    if (currentIndex_ < 0 || currentIndex_ >= items_.size()) {
        return QString();
    }
    return items_.at(currentIndex_).value;
}

void Select::setSize(Size size) {
    size_ = size;
    updateGeometry();
    update();
}

int Select::heightForSize() const {
    switch (size_) {
        case Size::Xs:
            return 26;
        case Size::Sm:
            return 32;
        case Size::Default:
        default:
            return 36;
    }
}

QSize Select::sizeHint() const {
    const QFontMetrics metrics(font());
    int width = metrics.horizontalAdvance(placeholder_) + 48;
    for (const Item& item : items_) {
        width = qMax(width, metrics.horizontalAdvance(item.text) + 48);
    }
    return {qMax(width, size_ == Size::Xs ? 100 : 160), heightForSize()};
}

QSize Select::minimumSizeHint() const {
    return {120, heightForSize()};
}

void Select::enterEvent(QEnterEvent* event) {
    hovered_ = true;
    update();
    QWidget::enterEvent(event);
}

void Select::leaveEvent(QEvent* event) {
    hovered_ = false;
    update();
    QWidget::leaveEvent(event);
}

void Select::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && isEnabled()) {
        openPopup();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void Select::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter) {
        openPopup();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Down && currentIndex_ + 1 < items_.size()) {
        setCurrentIndex(currentIndex_ + 1);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Up && currentIndex_ > 0) {
        setCurrentIndex(currentIndex_ - 1);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void Select::openPopup() {
    if (items_.isEmpty()) {
        return;
    }
    auto* popup = new SelectPopup(this, items_, currentIndex_, QWidget::size());
    const QPoint global = mapToGlobal(QPoint(0, height() + 4));
    popup->move(global);
    popup->show();
}

void Select::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    Fonts::preparePainter(&painter);

    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(bounds, Theme::radiusMd(), Theme::radiusMd());

    QColor fill = Theme::inputFill();
    if (hovered_) {
        fill = Theme::hover();
    }
    painter.fillPath(path, fill);

    QColor border = hasFocus() ? Theme::ring() : Theme::border();
    painter.setPen(QPen(border, hasFocus() ? 1.4 : 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    if (!isEnabled()) {
        painter.setOpacity(0.5);
    }

    const bool placeholder = currentIndex_ < 0;
    painter.setPen(placeholder ? Theme::mutedForeground() : Theme::foreground());
    painter.setFont(font());
    QRect textRect = rect().adjusted(10, 0, -28, 0);
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                     placeholder ? placeholder_ : currentText());

    const QPixmap chevron =
        Icons::pixmap(Icons::stroke(QStringLiteral("chevron-down")), 14, Theme::mutedForeground(),
                      devicePixelRatioF());
    painter.drawPixmap(width() - 22, (height() - 14) / 2, chevron);
}

}  // namespace edgeqt
