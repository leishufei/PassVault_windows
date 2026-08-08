#pragma once

#include <QDialog>
#include <QString>

#include "master_password/password_policy.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

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

 private slots:
    void OnAccept();
    void OnTogglePreview(bool visible);

 private:
    void BuildUi();
    void ValidateInputs();

    Mode mode_;
    QLabel* title_ = nullptr;
    QLabel* subtitle_ = nullptr;
    QLabel* error_ = nullptr;
    QLineEdit* old_edit_ = nullptr;
    QLineEdit* new_edit_ = nullptr;
    QLineEdit* confirm_edit_ = nullptr;
    QCheckBox* preview_ = nullptr;
    QPushButton* hello_button_ = nullptr;
    QPushButton* primary_ = nullptr;
};

}  // namespace passvault::ui
