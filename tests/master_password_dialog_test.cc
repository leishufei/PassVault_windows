#include <gtest/gtest.h>

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>

#include "ui/master_password_dialog.h"

namespace {

using passvault::ui::MasterPasswordDialog;
using Mode = MasterPasswordDialog::Mode;

QLineEdit* Edit(MasterPasswordDialog& d, const char* name) {
    return d.findChild<QLineEdit*>(QString::fromLatin1(name));
}
QPushButton* Button(MasterPasswordDialog& d, const char* name) {
    return d.findChild<QPushButton*>(QString::fromLatin1(name));
}
QLabel* ErrorLabel(MasterPasswordDialog& d) {
    return d.findChild<QLabel*>(QStringLiteral("FormError"));
}

void ExpectPasswordInputsEmpty(Mode mode) {
    MasterPasswordDialog dialog(mode);
    const auto edits = dialog.findChildren<QLineEdit*>();
    ASSERT_FALSE(edits.isEmpty());
    for (const auto* edit : edits) {
        EXPECT_TRUE(edit->text().isEmpty());
    }
}

TEST(MasterPasswordDialogTest, PasswordInputsStartEmptyForEveryMode) {
    ExpectPasswordInputsEmpty(Mode::kSetup);
    ExpectPasswordInputsEmpty(Mode::kUnlock);
    ExpectPasswordInputsEmpty(Mode::kChange);
}

TEST(MasterPasswordDialogTest, SetupAcceptsMatchingLongEnough) {
    MasterPasswordDialog d(Mode::kSetup);
    const QString input(MasterPasswordDialog::kMinPasswordLength,
                        QLatin1Char('n'));
    Edit(d, "NewPasswordEdit")->setText(input);
    Edit(d, "ConfirmPasswordEdit")->setText(input);
    QSignalSpy accepted(&d, &QDialog::accepted);
    Button(d, "PrimaryButton")->click();
    EXPECT_EQ(accepted.count(), 1);
    EXPECT_EQ(d.NewPassword(), input);
}

TEST(MasterPasswordDialogTest, SetupRejectsMismatch) {
    MasterPasswordDialog d(Mode::kSetup);
    const QString new_input(MasterPasswordDialog::kMinPasswordLength,
                            QLatin1Char('n'));
    const QString confirm_input(MasterPasswordDialog::kMinPasswordLength,
                                QLatin1Char('c'));
    Edit(d, "NewPasswordEdit")->setText(new_input);
    Edit(d, "ConfirmPasswordEdit")->setText(confirm_input);
    QSignalSpy accepted(&d, &QDialog::accepted);
    Button(d, "PrimaryButton")->click();
    EXPECT_EQ(accepted.count(), 0);
    EXPECT_FALSE(ErrorLabel(d)->isHidden());
}

TEST(MasterPasswordDialogTest, SetupRejectsTooShort) {
    MasterPasswordDialog d(Mode::kSetup);
    const QString input(MasterPasswordDialog::kMinPasswordLength - 1,
                        QLatin1Char('n'));
    Edit(d, "NewPasswordEdit")->setText(input);
    Edit(d, "ConfirmPasswordEdit")->setText(input);
    QSignalSpy accepted(&d, &QDialog::accepted);
    Button(d, "PrimaryButton")->click();
    EXPECT_EQ(accepted.count(), 0);
    EXPECT_FALSE(ErrorLabel(d)->isHidden());
}

TEST(MasterPasswordDialogTest, UnlockHelloButtonHiddenByDefault) {
    MasterPasswordDialog d(Mode::kUnlock);
    EXPECT_TRUE(Button(d, "HelloButton")->isHidden());
}

TEST(MasterPasswordDialogTest, UnlockHelloButtonEmitsSignal) {
    MasterPasswordDialog d(Mode::kUnlock);
    d.SetHelloAvailable(true);
    QSignalSpy spy(&d, &MasterPasswordDialog::HelloRequested);
    Button(d, "HelloButton")->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST(MasterPasswordDialogTest, UnlockHasNoConfirmField) {
    MasterPasswordDialog d(Mode::kUnlock);
    EXPECT_EQ(Edit(d, "ConfirmPasswordEdit"), nullptr);
    EXPECT_EQ(Edit(d, "OldPasswordEdit"), nullptr);
}

TEST(MasterPasswordDialogTest, UnlockAcceptsExistingShortPassword) {
    MasterPasswordDialog d(Mode::kUnlock);
    Edit(d, "NewPasswordEdit")->setText(QStringLiteral("x"));
    QSignalSpy accepted(&d, &QDialog::accepted);
    Button(d, "PrimaryButton")->click();
    EXPECT_EQ(accepted.count(), 1);
}

TEST(MasterPasswordDialogTest, ChangeReturnsOldAndNew) {
    MasterPasswordDialog d(Mode::kChange);
    const QString old_input = QStringLiteral("existing");
    const QString new_input(MasterPasswordDialog::kMinPasswordLength,
                            QLatin1Char('n'));
    Edit(d, "OldPasswordEdit")->setText(old_input);
    Edit(d, "NewPasswordEdit")->setText(new_input);
    Edit(d, "ConfirmPasswordEdit")->setText(new_input);
    QSignalSpy accepted(&d, &QDialog::accepted);
    Button(d, "PrimaryButton")->click();
    EXPECT_EQ(accepted.count(), 1);
    EXPECT_EQ(d.OldPassword(), old_input);
    EXPECT_EQ(d.NewPassword(), new_input);
}

TEST(MasterPasswordDialogTest, ChangeRejectsMismatchedConfirm) {
    MasterPasswordDialog d(Mode::kChange);
    const QString new_input(MasterPasswordDialog::kMinPasswordLength,
                            QLatin1Char('n'));
    const QString confirm_input(MasterPasswordDialog::kMinPasswordLength,
                                QLatin1Char('c'));
    Edit(d, "OldPasswordEdit")->setText(QStringLiteral("existing"));
    Edit(d, "NewPasswordEdit")->setText(new_input);
    Edit(d, "ConfirmPasswordEdit")->setText(confirm_input);
    QSignalSpy accepted(&d, &QDialog::accepted);
    Button(d, "PrimaryButton")->click();
    EXPECT_EQ(accepted.count(), 0);
    EXPECT_FALSE(ErrorLabel(d)->isHidden());
}

TEST(MasterPasswordDialogTest, ChangeRejectsNewPasswordBelowMinimum) {
    MasterPasswordDialog d(Mode::kChange);
    const QString input(MasterPasswordDialog::kMinPasswordLength - 1,
                        QLatin1Char('n'));
    Edit(d, "OldPasswordEdit")->setText(QStringLiteral("existing"));
    Edit(d, "NewPasswordEdit")->setText(input);
    Edit(d, "ConfirmPasswordEdit")->setText(input);
    QSignalSpy accepted(&d, &QDialog::accepted);
    Button(d, "PrimaryButton")->click();
    EXPECT_EQ(accepted.count(), 0);
    EXPECT_FALSE(ErrorLabel(d)->isHidden());
}

TEST(MasterPasswordDialogTest, ChangeRejectsSameAsOld) {
    MasterPasswordDialog d(Mode::kChange);
    const QString input(MasterPasswordDialog::kMinPasswordLength,
                        QLatin1Char('s'));
    Edit(d, "OldPasswordEdit")->setText(input);
    Edit(d, "NewPasswordEdit")->setText(input);
    Edit(d, "ConfirmPasswordEdit")->setText(input);
    QSignalSpy accepted(&d, &QDialog::accepted);
    Button(d, "PrimaryButton")->click();
    EXPECT_EQ(accepted.count(), 0);
    EXPECT_FALSE(ErrorLabel(d)->isHidden());
}

TEST(MasterPasswordDialogTest, TogglePreviewSwitchesEchoMode) {
    MasterPasswordDialog d(Mode::kSetup);
    auto* newEdit = Edit(d, "NewPasswordEdit");
    EXPECT_EQ(newEdit->echoMode(), QLineEdit::Password);
    auto* preview = d.findChild<QCheckBox*>(QStringLiteral("PreviewCheckbox"));
    ASSERT_NE(preview, nullptr);
    preview->setChecked(true);
    EXPECT_EQ(newEdit->echoMode(), QLineEdit::Normal);
    preview->setChecked(false);
    EXPECT_EQ(newEdit->echoMode(), QLineEdit::Password);
}

TEST(MasterPasswordDialogTest, SetErrorTextShowsMessage) {
    MasterPasswordDialog d(Mode::kUnlock);
    d.SetErrorText(QStringLiteral("validation error"));
    auto* err = ErrorLabel(d);
    ASSERT_NE(err, nullptr);
    EXPECT_FALSE(err->isHidden());
    EXPECT_EQ(err->text(), QStringLiteral("validation error"));
}

}  // namespace
