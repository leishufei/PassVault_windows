#include <gtest/gtest.h>

#include <memory>

#include <QTemporaryDir>
#include <QApplication>
#include <QFontInfo>
#include <QImage>
#include <QString>
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>

#include "oauth/google_oauth_client.h"
#include "oauth/token_store.h"
#include "ui/oauth_wizard_dialog.h"
#include "ui/theme_manager.h"

namespace {

using passvault::ui::OAuthWizardDialog;

class FakeOAuthClient : public passvault::oauth::GoogleOAuthClient {
 public:
    explicit FakeOAuthClient(passvault::oauth::TokenStore* ts)
        : passvault::oauth::GoogleOAuthClient(ts) {}
    void Authorize() override {}
    void CancelAuthorization() override { ++cancel_count; }
    int cancel_count = 0;
    void EmitSuccess() { emit AuthorizationSucceeded(); }
    void EmitFailure(const QString& m) { emit AuthorizationFailed(m); }
};

QPushButton* FindButton(OAuthWizardDialog* dlg, const QString& text) {
    const auto buttons = dlg->findChildren<QPushButton*>();
    for (auto* b : buttons) {
        if (b->text() == text) return b;
    }
    return nullptr;
}

QLabel* StatusLabel(OAuthWizardDialog* dlg) {
    return dlg->findChild<QLabel*>(QStringLiteral("OAuthStatus"));
}

class OAuthWizardDialogTest : public ::testing::Test {
 public:
    OAuthWizardDialogTest() {
        dir_ = std::make_unique<QTemporaryDir>();
        store_ = std::make_unique<passvault::oauth::TokenStore>(
            dir_->filePath(QStringLiteral("google_token.dat")));
        client_ = std::make_unique<FakeOAuthClient>(store_.get());
        dialog_ = std::make_unique<OAuthWizardDialog>(client_.get());
    }

    std::unique_ptr<QTemporaryDir> dir_;
    std::unique_ptr<passvault::oauth::TokenStore> store_;
    std::unique_ptr<FakeOAuthClient> client_;
    std::unique_ptr<OAuthWizardDialog> dialog_;
};

TEST_F(OAuthWizardDialogTest, IdleStateShowsStartAndCancel) {
    auto* start = FindButton(dialog_.get(), QStringLiteral("开始授权"));
    auto* cancel = FindButton(dialog_.get(), QStringLiteral("取消"));
    ASSERT_NE(start, nullptr);
    ASSERT_NE(cancel, nullptr);
    EXPECT_EQ(start->text(), QStringLiteral("开始授权"));
    EXPECT_TRUE(start->isEnabled());
    EXPECT_FALSE(cancel->isHidden());
    QSignalSpy spy(dialog_.get(), &OAuthWizardDialog::ConnectedSuccessfully);
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(OAuthWizardDialogTest, DescriptionUsesSharedTypographyAndTransparentCorners) {
    auto* theme = passvault::ui::ThemeManager::Instance();
    const auto previous = theme->theme();
    for (auto value : {passvault::ui::Theme::kLight,
                       passvault::ui::Theme::kDark}) {
        theme->ApplyTheme(value);
        dialog_->show();
        QApplication::processEvents();
        auto* description = dialog_->findChild<QLabel*>("DialogSubtitle");
        ASSERT_NE(description, nullptr);
        EXPECT_EQ(QFontInfo(description->font()).family(),
                  QFontInfo(QApplication::font()).family());
        EXPECT_EQ(QFontInfo(description->font()).pixelSize(), 13);
        EXPECT_EQ(dialog_->grab().toImage().pixelColor(0, 0).alpha(), 0);
        auto* card = dialog_->findChild<QWidget*>(QStringLiteral("AppDialogCard"));
        ASSERT_NE(card, nullptr);
        EXPECT_EQ(dialog_->grab().toImage().pixelColor(
                      card->geometry().center().x(), card->geometry().top() + 12),
                  theme->Color(QStringLiteral("card")));
        const QString capture = qEnvironmentVariable("PASSVAULT_UI_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            dialog_->grab().save(capture + (value == passvault::ui::Theme::kLight
                                               ? "/oauth-light.png" : "/oauth-dark.png"));
        }
    }
    theme->ApplyTheme(previous);
}

TEST_F(OAuthWizardDialogTest, StartEntersWaitingState) {
    auto* start = FindButton(dialog_.get(), QStringLiteral("开始授权"));
    ASSERT_NE(start, nullptr);
    start->click();
    EXPECT_TRUE(StatusLabel(dialog_.get())
                    ->text().contains(QStringLiteral("等待授权")));
    EXPECT_FALSE(start->isEnabled());
}

TEST_F(OAuthWizardDialogTest, FailureShowsRetry) {
    client_->EmitFailure(QStringLiteral("x"));
    auto* start = FindButton(dialog_.get(), QStringLiteral("重试"));
    ASSERT_NE(start, nullptr);
    EXPECT_TRUE(StatusLabel(dialog_.get())
                    ->text().contains(QStringLiteral("授权失败")));
    EXPECT_TRUE(start->isEnabled());
}

TEST_F(OAuthWizardDialogTest, RetryReentersWaiting) {
    client_->EmitFailure(QStringLiteral("x"));
    auto* start = FindButton(dialog_.get(), QStringLiteral("重试"));
    ASSERT_NE(start, nullptr);
    start->click();
    EXPECT_TRUE(StatusLabel(dialog_.get())
                    ->text().contains(QStringLiteral("等待授权")));
    EXPECT_FALSE(start->isEnabled());
}

TEST_F(OAuthWizardDialogTest, SuccessEmitsConnectedAndAccepts) {
    QSignalSpy spy(dialog_.get(), &OAuthWizardDialog::ConnectedSuccessfully);
    client_->EmitSuccess();
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(dialog_->result(), int(QDialog::Accepted));
}

TEST_F(OAuthWizardDialogTest, CancelRejects) {
    QSignalSpy spy(dialog_.get(), &OAuthWizardDialog::ConnectedSuccessfully);
    auto* cancel = FindButton(dialog_.get(), QStringLiteral("取消"));
    ASSERT_NE(cancel, nullptr);
    cancel->click();
    EXPECT_EQ(dialog_->result(), int(QDialog::Rejected));
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(OAuthWizardDialogTest, CancelStopsClientAuthorization) {
    auto* cancel = FindButton(dialog_.get(), QStringLiteral("取消"));
    ASSERT_NE(cancel, nullptr);
    cancel->click();
    EXPECT_EQ(dialog_->result(), int(QDialog::Rejected));
    EXPECT_EQ(client_->cancel_count, 1);
}

TEST_F(OAuthWizardDialogTest, CancelDuringWaitingStillRejects) {
    QSignalSpy spy(dialog_.get(), &OAuthWizardDialog::ConnectedSuccessfully);
    auto* start = FindButton(dialog_.get(), QStringLiteral("开始授权"));
    ASSERT_NE(start, nullptr);
    start->click();
    ASSERT_FALSE(start->isEnabled());
    auto* cancel = FindButton(dialog_.get(), QStringLiteral("取消"));
    ASSERT_NE(cancel, nullptr);
    cancel->click();
    EXPECT_EQ(client_->cancel_count, 1);
    EXPECT_EQ(dialog_->result(), int(QDialog::Rejected));
    EXPECT_EQ(spy.count(), 0);
}

TEST(OAuthWizardDialogNullTest, NullClientStaysIdle) {
    OAuthWizardDialog dialog(nullptr);
    auto* start = FindButton(&dialog, QStringLiteral("开始授权"));
    ASSERT_NE(start, nullptr);
    start->click();
    EXPECT_TRUE(start->isEnabled());
    EXPECT_TRUE(StatusLabel(&dialog)->text().isEmpty());
}

TEST_F(OAuthWizardDialogTest, CardWidthIsFigma) {
    auto* card = dialog_->findChild<QWidget*>(QStringLiteral("AppDialogCard"));
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->minimumWidth(), 480);
    EXPECT_EQ(card->maximumWidth(), 480);
}

TEST_F(OAuthWizardDialogTest, EscapeAlsoStopsClientAuthorization) {
    dialog_->reject();
    EXPECT_EQ(client_->cancel_count, 1);
    EXPECT_EQ(dialog_->result(), int(QDialog::Rejected));
}

}  // namespace
