#pragma once

#include <QLabel>
#include <QString>

class QEvent;
class QResizeEvent;
class QWidget;

namespace passvault::ui {

class ElidedLabel final : public QLabel {
 public:
    explicit ElidedLabel(QWidget* parent = nullptr);
    ElidedLabel(const QString& text, QWidget* parent = nullptr);

    void SetFullText(const QString& text);

 protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

 private:
    void UpdateElidedText();

    QString full_text_;
};

}  // namespace passvault::ui
