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

TEST(MasterPasswordDialogTest, SetupAcceptsMatchingLongEnough) {
    MasterPasswordDialog d(Mode::kSetup);
    Edit(d, "NewPasswordEdit")->setText(QStringLiteral("password123"));
    Edit(d, "ConfirmPasswordEdit")->setText(QStringLiteral("password123"));
    QSignalSpy accepted(&d, &QDialog::accepted);
    Button(d, "PrimaryButton")->click();
    EXPECT_EQ(accepted.count(), 1);
    EXPECT_EQ(d.NewPassword(), QStringLiteral("password123"));
}

TEST(MasterPasswordDialogTest, SetupRejectsMismatch) {
    MasterPasswordDialog d(Mode::kSetup);
    Edit(d, "NewPasswordEdit")->setText(QStringLiteral("password123"));
    Edit(d, "ConfirmPasswordEdit")->setText(QStringLiteral("different99"));
    QSignalSpy accepted(&d, &QDialog::accepted);
    Button(d, "PrimaryButton")->click();
    EXPECT_EQ(accepted.count(), 0);
    EXPECT_FALSE(ErrorLabel(d)->isHidden());
}

TEST(MasterPasswordDialogTest, SetupRejectsTooShort) {
    MasterPasswordDialog d(Mode::kSetup);
    Edit(d, "NewPasswordEdit")->setText(QStringLiteral("short"));
    Edit(d, "ConfirmPasswordEdit")->setText(QStringLiteral("short"));
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

TEST(MasterPasswordDialogTest, ChangeReturnsOldAndNew) {
    MasterPasswordDialog d(Mode::kChange);
    Edit(d, "OldPasswordEdit")->setText(QStringLiteral("oldpassword"));
    Edit(d, "NewPasswordEdit")->setText(QStringLiteral("newpassword"));
    Edit(d, "ConfirmPasswordEdit")->setText(QStringLiteral("newpassword"));
    QSignalSpy accepted(&d, &QDialog::accepted);
    Button(d, "PrimaryButton")->click();
    EXPECT_EQ(accepted.count(), 1);
    EXPECT_EQ(d.OldPassword(), QStringLiteral("oldpassword"));
    EXPECT_EQ(d.NewPassword(), QStringLiteral("newpassword"));
}

TEST(MasterPasswordDialogTest, ChangeRejectsMismatchedConfirm) {
    MasterPasswordDialog d(Mode::kChange);
    Edit(d, "OldPasswordEdit")->setText(QStringLiteral("oldpassword"));
    Edit(d, "NewPasswordEdit")->setText(QStringLiteral("newpassword"));
    Edit(d, "ConfirmPasswordEdit")->setText(QStringLiteral("mismatch123"));
    QSignalSpy accepted(&d, &QDialog::accepted);
    Button(d, "PrimaryButton")->click();
    EXPECT_EQ(accepted.count(), 0);
    EXPECT_FALSE(ErrorLabel(d)->isHidden());
}

TEST(MasterPasswordDialogTest, ChangeRejectsSameAsOld) {
    MasterPasswordDialog d(Mode::kChange);
    Edit(d, "OldPasswordEdit")->setText(QStringLiteral("samepassword"));
    Edit(d, "NewPasswordEdit")->setText(QStringLiteral("samepassword"));
    Edit(d, "ConfirmPasswordEdit")->setText(QStringLiteral("samepassword"));
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
    d.SetErrorText(QStringLiteral("bad password"));
    auto* err = ErrorLabel(d);
    ASSERT_NE(err, nullptr);
    EXPECT_FALSE(err->isHidden());
    EXPECT_EQ(err->text(), QStringLiteral("bad password"));
}

}  // namespace
