#include "ui/elided_label.h"

#include <QEvent>
#include <QFontMetrics>
#include <QResizeEvent>
#include <QSizePolicy>

namespace passvault::ui {

ElidedLabel::ElidedLabel(QWidget* parent) : QLabel(parent) {
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
}

ElidedLabel::ElidedLabel(const QString& text, QWidget* parent)
    : ElidedLabel(parent) {
    SetFullText(text);
}

void ElidedLabel::SetFullText(const QString& text) {
    full_text_ = text;
    setToolTip(full_text_);
    setAccessibleName(full_text_);
    UpdateElidedText();
}

void ElidedLabel::changeEvent(QEvent* event) {
    QLabel::changeEvent(event);
    if (event->type() == QEvent::FontChange ||
        event->type() == QEvent::StyleChange) {
        UpdateElidedText();
    }
}

void ElidedLabel::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    UpdateElidedText();
}

void ElidedLabel::UpdateElidedText() {
    const QString visible_text = fontMetrics().elidedText(
        full_text_, Qt::ElideRight, contentsRect().width());
    if (text() != visible_text) QLabel::setText(visible_text);
}

}  // namespace passvault::ui
