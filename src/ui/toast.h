#pragma once

#include <QFrame>
#include <QString>

class QLabel;
class QPropertyAnimation;
class QTimer;

namespace passvault::ui {

class Toast : public QFrame {
    Q_OBJECT

 public:
    enum class Level { kInfo, kSuccess, kWarning, kError };

    static void Show(QWidget* parent, const QString& text,
                     Level level = Level::kInfo, int duration_ms = 2400);

 private:
    Toast(QWidget* parent, const QString& text, Level level, int duration_ms);
    void FadeOutAndClose();
    void PositionOverParent();

    QLabel* label_ = nullptr;
    QTimer* dismiss_ = nullptr;
    QPropertyAnimation* animation_ = nullptr;
};

}  // namespace passvault::ui
