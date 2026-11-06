#include "ui/toast.h"

#include <QEvent>
#include <QFontMetricsF>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>

#include <cmath>

namespace passvault::ui {

namespace {

constexpr int kMaxWidth = 480;

const char* LevelObjectName(Toast::Level level) {
    switch (level) {
        case Toast::Level::kSuccess:
            return "ToastSuccess";
        case Toast::Level::kWarning:
            return "ToastWarning";
        case Toast::Level::kError:
            return "ToastError";
        case Toast::Level::kInfo:
        default:
            return "Toast";
    }
}

}  // namespace

void Toast::Show(QWidget* parent, const QString& text, Level level,
                 int duration_ms) {
    if (!parent) return;
    const auto previous = parent->findChildren<Toast*>(
        QString(), Qt::FindDirectChildrenOnly);
    for (auto* toast : previous) toast->close();
    auto* toast = new Toast(parent, text, level, duration_ms);
    toast->show();
    toast->raise();
}

Toast::Toast(QWidget* parent, const QString& text, Level level, int duration_ms)
    : QFrame(parent) {
    setObjectName(QString::fromLatin1(LevelObjectName(level)));
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    setFrameShape(QFrame::NoFrame);

    label_ = new QLabel(text, this);
    label_->setObjectName(QStringLiteral("ToastText"));
    label_->setTextFormat(Qt::PlainText);
    label_->setWordWrap(true);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->addWidget(label_);

    auto* effect = new QGraphicsOpacityEffect(this);
    effect->setOpacity(0.0);
    setGraphicsEffect(effect);

    animation_ = new QPropertyAnimation(effect, "opacity", this);
    animation_->setDuration(180);
    animation_->setStartValue(0.0);
    animation_->setEndValue(1.0);
    animation_->start();

    dismiss_ = new QTimer(this);
    dismiss_->setSingleShot(true);
    connect(dismiss_, &QTimer::timeout, this, &Toast::FadeOutAndClose);
    dismiss_->start(duration_ms);

    parent->installEventFilter(this);
    PositionOverParent();
}

void Toast::FadeOutAndClose() {
    animation_->stop();
    auto* effect = qobject_cast<QGraphicsOpacityEffect*>(graphicsEffect());
    if (!effect) {
        close();
        return;
    }
    auto* out = new QPropertyAnimation(effect, "opacity", this);
    out->setDuration(200);
    out->setStartValue(effect->opacity());
    out->setEndValue(0.0);
    connect(out, &QPropertyAnimation::finished, this, &QWidget::close);
    out->start(QAbstractAnimation::DeleteWhenStopped);
}

bool Toast::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parentWidget() &&
        (event->type() == QEvent::Resize ||
         event->type() == QEvent::StyleChange)) {
        PositionOverParent();
    }
    return QFrame::eventFilter(watched, event);
}

void Toast::PositionOverParent() {
    QWidget* p = parentWidget();
    if (!p) return;
    // QSS sets the label font and the frame border; measure with them.
    ensurePolished();
    label_->ensurePolished();
    // Sizing from sizeHint() left the wrapping label a few pixels short of
    // one line (the QSS border and font are not in the hint), so the last
    // glyph of short messages wrapped. Size to the measured single-line width
    // and only wrap once the toast reaches its maximum width.
    const QFontMetricsF metrics(label_->font());
    qreal text_width = 0;
    for (const auto& line : label_->text().split(QLatin1Char('\n'))) {
        text_width = qMax(text_width, metrics.horizontalAdvance(line));
    }
    const QMargins frame = contentsMargins();
    const QMargins padding = layout()->contentsMargins();
    const int chrome = frame.left() + frame.right() + padding.left() +
                       padding.right() + label_->contentsMargins().left() +
                       label_->contentsMargins().right();
    // +4 covers the 1px QSS border on each side plus glyph rounding.
    const int natural = static_cast<int>(std::ceil(text_width)) + chrome + 4;
    const int max_w = qMax(1, qMin(p->width() - 40, kMaxWidth));
    setFixedWidth(qBound(1, natural, max_w));
    const int for_width = layout()->totalHeightForWidth(width());
    const int content_height =
        for_width > 0 ? for_width : layout()->totalSizeHint().height();
    setFixedHeight(qBound(1, content_height, qMax(1, p->height() - 16)));
    const int x = (p->width() - width()) / 2;
    const int y = qMax(0, p->height() - this->height() - 32);
    move(x, y);
}

}  // namespace passvault::ui
