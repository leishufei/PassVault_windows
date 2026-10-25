#pragma once

#include <QDialog>
#include <QPointer>
#include <QString>

#include "master_password/password_policy.h"

class QCheckBox;
class QGraphicsDropShadowEffect;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace passvault::ui {

class MasterPasswordDialog : public QDialog {
    Q_OBJECT

 public:
    enum class Mode {
        kSetup,   // first-time creation: new + confirm
        kUnlock,  // single input; optional Windows Hello button
        kChange,  // old + new + confirm
    };

    static constexpr int kMinPasswordLength =
        master_password::kMinNewPasswordLength;

    explicit MasterPasswordDialog(Mode mode, QWidget* parent = nullptr);
    ~MasterPasswordDialog() override;

    QString OldPassword() const;
    QString NewPassword() const;

    void SetHelloAvailable(bool available);
    void SetErrorText(const QString& text);

 signals:
    void HelloRequested();

 protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

 private slots:
    void OnAccept();
    void OnTogglePreview(bool visible);

 private:
    void BuildUi();
    void RefreshThemeAssets();
    void ValidateInputs();

    Mode mode_;
    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QLabel* icon_ = nullptr;
    QGraphicsDropShadowEffect* shadow_ = nullptr;
    QPointer<QWidget> backdrop_;
    QToolButton* close_button_ = nullptr;
    QLabel* error_ = nullptr;
    QLineEdit* old_edit_ = nullptr;
    QLineEdit* new_edit_ = nullptr;
    QLineEdit* confirm_edit_ = nullptr;
    QCheckBox* preview_ = nullptr;
    QPushButton* hello_button_ = nullptr;
    QPushButton* primary_ = nullptr;
};

}  // namespace passvault::ui
