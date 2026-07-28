#include "ui/toast.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QTimer>

namespace passvault::ui {

namespace {

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

void Toast::PositionOverParent() {
    QWidget* p = parentWidget();
    if (!p) return;
    adjustSize();
    const int max_w = qMin(p->width() - 40, 480);
    setFixedWidth(qMin(sizeHint().width(), max_w));
    const int x = (p->width() - width()) / 2;
    const int y = p->height() - height() - 32;
    move(x, y);
}

}  // namespace passvault::ui
