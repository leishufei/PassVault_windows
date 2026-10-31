#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSettings>
#include <QTest>
#include <QToolButton>

#include "ui/master_password_dialog.h"
#include "ui/theme_manager.h"

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
        EXPECT_EQ(edit->echoMode(), QLineEdit::Password);
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

TEST(MasterPasswordDialogTest, HelloEntryIsRemovedInAllModes) {
    for (const Mode mode : {Mode::kSetup, Mode::kUnlock, Mode::kChange}) {
        MasterPasswordDialog dialog(mode);
        EXPECT_EQ(Button(dialog, "HelloButton"), nullptr);
    }
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

TEST(MasterPasswordDialogTest, EmptySubmissionFocusesFirstRequiredField) {
    for (const Mode mode : {Mode::kSetup, Mode::kUnlock, Mode::kChange}) {
        SCOPED_TRACE(static_cast<int>(mode));
        MasterPasswordDialog dialog(mode);
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        Button(dialog, "PrimaryButton")->click();
        EXPECT_EQ(accepted.count(), 0);
        EXPECT_FALSE(ErrorLabel(dialog)->isHidden());
        EXPECT_EQ(dialog.focusWidget(),
                  Edit(dialog, mode == Mode::kChange ? "OldPasswordEdit"
                                                    : "NewPasswordEdit"));
    }
}

TEST(MasterPasswordDialogTest, CorrectingMismatchClearsErrorAndAllowsRetry) {
    MasterPasswordDialog dialog(Mode::kChange);
    const QString input(MasterPasswordDialog::kMinPasswordLength,
                        QLatin1Char('n'));
    Edit(dialog, "OldPasswordEdit")->setText(QStringLiteral("existing"));
    Edit(dialog, "NewPasswordEdit")->setText(input);
    Edit(dialog, "ConfirmPasswordEdit")->setText(QStringLiteral("mismatch"));
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    Button(dialog, "PrimaryButton")->click();
    EXPECT_EQ(accepted.count(), 0);
    EXPECT_EQ(dialog.focusWidget(), Edit(dialog, "ConfirmPasswordEdit"));
    EXPECT_FALSE(ErrorLabel(dialog)->isHidden());

    Edit(dialog, "ConfirmPasswordEdit")->setText(input);
    EXPECT_TRUE(ErrorLabel(dialog)->isHidden());
    Button(dialog, "PrimaryButton")->click();
    EXPECT_EQ(accepted.count(), 1);
}

TEST(MasterPasswordDialogTest, EnterSubmitsPrimaryActionForEveryMode) {
    for (const Mode mode : {Mode::kSetup, Mode::kUnlock, Mode::kChange}) {
        SCOPED_TRACE(static_cast<int>(mode));
        MasterPasswordDialog dialog(mode);
        const QString input(MasterPasswordDialog::kMinPasswordLength,
                            QLatin1Char('n'));
        if (auto* old_edit = Edit(dialog, "OldPasswordEdit")) {
            old_edit->setText(QStringLiteral("existing"));
        }
        Edit(dialog, "NewPasswordEdit")->setText(input);
        auto* last_edit = Edit(dialog, "ConfirmPasswordEdit");
        if (last_edit) {
            last_edit->setText(input);
        } else {
            last_edit = Edit(dialog, "NewPasswordEdit");
        }
        dialog.show();
        QApplication::processEvents();
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        QSignalSpy rejected(&dialog, &QDialog::rejected);
        QTest::keyClick(last_edit, Qt::Key_Return);
        EXPECT_EQ(accepted.count(), 1);
        EXPECT_EQ(rejected.count(), 0);
    }
}

TEST(MasterPasswordDialogTest, CancelCloseAndEscapeNeverAccept) {
    for (const Mode mode : {Mode::kSetup, Mode::kUnlock, Mode::kChange}) {
        for (int action = 0; action < 3; ++action) {
            SCOPED_TRACE(static_cast<int>(mode));
            SCOPED_TRACE(action);
            MasterPasswordDialog dialog(mode);
            QSignalSpy accepted(&dialog, &QDialog::accepted);
            QSignalSpy rejected(&dialog, &QDialog::rejected);
            if (action == 0) {
                Button(dialog, "CancelButton")->click();
            } else if (action == 1) {
                auto* close = dialog.findChild<QToolButton*>(
                    QStringLiteral("MasterPasswordClose"));
                ASSERT_NE(close, nullptr);
                close->click();
            } else {
                QTest::keyClick(&dialog, Qt::Key_Escape);
            }
            EXPECT_EQ(accepted.count(), 0);
            EXPECT_EQ(rejected.count(), 1);
        }
    }
}

TEST(MasterPasswordDialogTest, EnterActivatesFocusedCancelButton) {
    for (const char* name : {"CancelButton"}) {
        SCOPED_TRACE(name);
        MasterPasswordDialog dialog(Mode::kUnlock);
        Edit(dialog, "NewPasswordEdit")->setText(QStringLiteral("existing"));
        dialog.show();
        QApplication::processEvents();
        auto* button = Button(dialog, name);
        ASSERT_NE(button, nullptr);
        button->setFocus();
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        QSignalSpy clicked(button, &QPushButton::clicked);
        QTest::keyClick(button, Qt::Key_Return);
        EXPECT_EQ(clicked.count(), 1);
        EXPECT_EQ(accepted.count(), 0);
    }
}

TEST(MasterPasswordDialogTest, TabOrderFollowsPasswordFieldsThenActions) {
    MasterPasswordDialog dialog(Mode::kChange);
    dialog.show();
    QApplication::processEvents();
    auto* old_edit = Edit(dialog, "OldPasswordEdit");
    auto* new_edit = Edit(dialog, "NewPasswordEdit");
    auto* confirm_edit = Edit(dialog, "ConfirmPasswordEdit");
    auto* preview = dialog.findChild<QCheckBox*>(QStringLiteral("PreviewCheckbox"));
    ASSERT_NE(preview, nullptr);
    EXPECT_EQ(dialog.focusWidget(), old_edit);
    QTest::keyClick(old_edit, Qt::Key_Tab);
    EXPECT_EQ(dialog.focusWidget(), new_edit);
    QTest::keyClick(new_edit, Qt::Key_Tab);
    EXPECT_EQ(dialog.focusWidget(), confirm_edit);
    QTest::keyClick(confirm_edit, Qt::Key_Tab);
    EXPECT_EQ(dialog.focusWidget(), preview);
    QTest::keyClick(preview, Qt::Key_Tab);
    EXPECT_EQ(dialog.focusWidget(), Button(dialog, "CancelButton"));
    QTest::keyClick(Button(dialog, "CancelButton"), Qt::Key_Tab);
    EXPECT_EQ(dialog.focusWidget(), Button(dialog, "PrimaryButton"));
}

TEST(MasterPasswordDialogTest, BackdropTracksParentAndClosesWithDialog) {
    QWidget window;
    window.resize(1200, 800);
    window.show();
    {
        MasterPasswordDialog dialog(Mode::kChange, &window);
        dialog.show();
        QApplication::processEvents();
        auto* backdrop = window.findChild<QWidget*>(
            QStringLiteral("MasterPasswordBackdrop"));
        ASSERT_NE(backdrop, nullptr);
        EXPECT_TRUE(backdrop->isVisible());
        EXPECT_EQ(backdrop->geometry(), window.rect());
        window.resize(900, 700);
        QApplication::processEvents();
        EXPECT_EQ(backdrop->geometry(), window.rect());
        dialog.reject();
        EXPECT_TRUE(backdrop->isHidden());
    }
    EXPECT_EQ(window.findChild<QWidget*>(
                  QStringLiteral("MasterPasswordBackdrop")),
              nullptr);
}

TEST(MasterPasswordDialogTest, FooterButtonsKeepHeightAfterThemePolish) {
    auto* theme = passvault::ui::ThemeManager::Instance();
    const auto previous_theme = theme->theme();
    for (const Mode mode : {Mode::kSetup, Mode::kUnlock, Mode::kChange}) {
        MasterPasswordDialog dialog(mode);
        dialog.show();
        for (const auto appearance : {passvault::ui::Theme::kLight,
                                     passvault::ui::Theme::kDark}) {
            theme->ApplyTheme(appearance);
            QApplication::processEvents();
            EXPECT_EQ(Button(dialog, "CancelButton")->height(), 32);
            EXPECT_EQ(Button(dialog, "PrimaryButton")->height(), 32);
        }
    }
    theme->ApplyTheme(previous_theme);
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

TEST(MasterPasswordDialogTest, ThemeSwitchPreservesInputsPreviewAndError) {
    MasterPasswordDialog dialog(Mode::kChange);
    auto* close = dialog.findChild<QToolButton*>(
        QStringLiteral("MasterPasswordClose"));
    auto* preview = dialog.findChild<QCheckBox*>(QStringLiteral("PreviewCheckbox"));
    ASSERT_NE(close, nullptr);
    ASSERT_NE(preview, nullptr);
    auto* theme = passvault::ui::ThemeManager::Instance();
    const auto previous_theme = theme->theme();
    theme->ApplyTheme(passvault::ui::Theme::kLight);
    const auto light_icon = close->icon().pixmap(16, 16).toImage();
    const QString input(MasterPasswordDialog::kMinPasswordLength,
                        QLatin1Char('n'));
    Edit(dialog, "OldPasswordEdit")->setText(QStringLiteral("existing"));
    Edit(dialog, "NewPasswordEdit")->setText(input);
    Edit(dialog, "ConfirmPasswordEdit")->setText(input);
    preview->setChecked(true);
    dialog.SetErrorText(QStringLiteral("validation error"));

    theme->ApplyTheme(passvault::ui::Theme::kDark);

    EXPECT_NE(close->icon().pixmap(16, 16).toImage(), light_icon);
    EXPECT_EQ(dialog.OldPassword(), QStringLiteral("existing"));
    EXPECT_EQ(dialog.NewPassword(), input);
    EXPECT_TRUE(preview->isChecked());
    for (const auto* edit : dialog.findChildren<QLineEdit*>()) {
        EXPECT_EQ(edit->echoMode(), QLineEdit::Normal);
    }
    EXPECT_FALSE(ErrorLabel(dialog)->isHidden());
    preview->setChecked(false);
    for (const auto* edit : dialog.findChildren<QLineEdit*>()) {
        EXPECT_EQ(edit->echoMode(), QLineEdit::Password);
    }
    theme->ApplyTheme(previous_theme);
}

}  // namespace

TEST(MasterPasswordVisibilityIsolationTest, UiDefaultDoesNotExposeAuthenticationFields) {
    QSettings().setValue("ui/default_show_passwords", true);
    for (auto mode : {Mode::kSetup, Mode::kUnlock, Mode::kChange}) {
        MasterPasswordDialog dialog(mode);
        const auto fields = dialog.findChildren<QLineEdit*>();
        ASSERT_FALSE(fields.isEmpty());
        for (auto* field : fields) {
            EXPECT_EQ(field->echoMode(), QLineEdit::Password);
        }
    }
    QSettings().setValue("ui/default_show_passwords", false);
}
