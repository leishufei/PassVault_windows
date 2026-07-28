#include <gtest/gtest.h>

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTextEdit>
#include <QToolButton>

#include "model/category.h"
#include "model/password_entry.h"
#include "ui/editor_panel.h"

namespace {

using passvault::ui::EditorPanel;

class EditorPanelTest : public ::testing::Test {
 protected:
    EditorPanel panel_;

    QLineEdit* titleInput() {
        return panel_.findChild<QLineEdit*>(QStringLiteral("EditorTitleInput"));
    }
    QLineEdit* usernameInput() {
        return panel_.findChild<QLineEdit*>(QStringLiteral("EditorUsernameInput"));
    }
    QLineEdit* passwordInput() {
        return panel_.findChild<QLineEdit*>(QStringLiteral("EditorPasswordInput"));
    }
    QLineEdit* websiteInput() {
        return panel_.findChild<QLineEdit*>(QStringLiteral("EditorWebsiteInput"));
    }
    QTextEdit* notesInput() {
        return panel_.findChild<QTextEdit*>(QStringLiteral("EditorNotesInput"));
    }
    QComboBox* categoryCombo() {
        return panel_.findChild<QComboBox*>(QStringLiteral("EditorCategoryCombo"));
    }
    QLabel* headerTitle() {
        return panel_.findChild<QLabel*>(QStringLiteral("EditorHeaderTitle"));
    }
    QLabel* strengthLabel() {
        return panel_.findChild<QLabel*>(QStringLiteral("EditorStrengthLabel"));
    }
    QPushButton* saveButton() {
        return panel_.findChild<QPushButton*>(QStringLiteral("EditorSaveButton"));
    }
    QPushButton* cancelButton() {
        return panel_.findChild<QPushButton*>(QStringLiteral("EditorCancelButton"));
    }
    QToolButton* generateButton() {
        return panel_.findChild<QToolButton*>(QStringLiteral("EditorGenerateButton"));
    }
    QToolButton* previewToggle() {
        return panel_.findChild<QToolButton*>(QStringLiteral("EditorPreviewToggle"));
    }
};

TEST_F(EditorPanelTest, OpenForCreateClearsFieldsAndOpens) {
    titleInput()->setText(QStringLiteral("leftover"));
    panel_.OpenForCreate();
    EXPECT_TRUE(panel_.IsOpen());
    EXPECT_TRUE(titleInput()->text().isEmpty());
    EXPECT_TRUE(usernameInput()->text().isEmpty());
    EXPECT_TRUE(passwordInput()->text().isEmpty());
    EXPECT_EQ(headerTitle()->text(), QStringLiteral("新建密码"));
}

TEST_F(EditorPanelTest, OpenForEditFillsFields) {
    EditorPanel::DecryptedEntry e;
    e.entry.title = QStringLiteral("GitHub");
    e.entry.username = QStringLiteral("octocat");
    e.entry.website = QStringLiteral("https://github.com");
    e.entry.notes = QStringLiteral("work account");
    e.password = QStringLiteral("s3cr3tPass!");
    panel_.OpenForEdit(e);
    EXPECT_TRUE(panel_.IsOpen());
    EXPECT_EQ(titleInput()->text(), QStringLiteral("GitHub"));
    EXPECT_EQ(usernameInput()->text(), QStringLiteral("octocat"));
    EXPECT_EQ(websiteInput()->text(), QStringLiteral("https://github.com"));
    EXPECT_EQ(passwordInput()->text(), QStringLiteral("s3cr3tPass!"));
    EXPECT_EQ(notesInput()->toPlainText(), QStringLiteral("work account"));
    EXPECT_EQ(headerTitle()->text(), QStringLiteral("编辑密码"));
}

TEST_F(EditorPanelTest, ResultReflectsEditedFields) {
    panel_.OpenForCreate();
    titleInput()->setText(QStringLiteral("NewTitle"));
    usernameInput()->setText(QStringLiteral("user1"));
    passwordInput()->setText(QStringLiteral("pw123456"));
    websiteInput()->setText(QStringLiteral("https://example.com"));
    notesInput()->setPlainText(QStringLiteral("a note"));
    const auto r = panel_.Result();
    EXPECT_EQ(r.entry.title, QStringLiteral("NewTitle"));
    EXPECT_EQ(r.entry.username, QStringLiteral("user1"));
    EXPECT_EQ(r.password, QStringLiteral("pw123456"));
    EXPECT_EQ(r.entry.website, QStringLiteral("https://example.com"));
    EXPECT_EQ(r.entry.notes, QStringLiteral("a note"));
}

TEST_F(EditorPanelTest, SaveWithValidFieldsEmitsSignal) {
    panel_.OpenForCreate();
    titleInput()->setText(QStringLiteral("Title"));
    usernameInput()->setText(QStringLiteral("user"));
    passwordInput()->setText(QStringLiteral("password123"));
    QSignalSpy spy(&panel_, &EditorPanel::SaveRequested);
    saveButton()->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(EditorPanelTest, SaveWithEmptyTitleDoesNotEmit) {
    panel_.OpenForCreate();
    usernameInput()->setText(QStringLiteral("user"));
    passwordInput()->setText(QStringLiteral("password123"));
    QSignalSpy spy(&panel_, &EditorPanel::SaveRequested);
    saveButton()->click();
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(EditorPanelTest, CancelButtonEmitsSignal) {
    panel_.OpenForCreate();
    QSignalSpy spy(&panel_, &EditorPanel::CancelRequested);
    cancelButton()->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(EditorPanelTest, GenerateButtonEmitsSignal) {
    panel_.OpenForCreate();
    QSignalSpy spy(&panel_, &EditorPanel::GenerateRequested);
    generateButton()->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(EditorPanelTest, ApplyGeneratedPasswordSetsField) {
    panel_.OpenForCreate();
    panel_.ApplyGeneratedPassword(QStringLiteral("Xy9!mkQ2pL"));
    EXPECT_EQ(passwordInput()->text(), QStringLiteral("Xy9!mkQ2pL"));
}

TEST_F(EditorPanelTest, PasswordChangeUpdatesStrengthLabel) {
    panel_.OpenForCreate();
    passwordInput()->setText(QStringLiteral("A1b!c9D#eF2xY"));
    EXPECT_EQ(strengthLabel()->text(), QStringLiteral("强度：强"));
}

TEST_F(EditorPanelTest, PreviewToggleSwitchesEchoMode) {
    panel_.OpenForCreate();
    EXPECT_EQ(passwordInput()->echoMode(), QLineEdit::Password);
    previewToggle()->setChecked(true);
    EXPECT_EQ(passwordInput()->echoMode(), QLineEdit::Normal);
    previewToggle()->setChecked(false);
    EXPECT_EQ(passwordInput()->echoMode(), QLineEdit::Password);
}

TEST_F(EditorPanelTest, CategoryComboReflectsCategories) {
    QList<passvault::model::Category> cats;
    passvault::model::Category c;
    c.id = 42;
    c.name = QStringLiteral("Work");
    cats.append(c);
    panel_.SetCategories(cats);
    auto* combo = categoryCombo();
    ASSERT_NE(combo, nullptr);
    // "未分类" (id 0) + "Work" (id 42)
    EXPECT_EQ(combo->count(), 2);
    bool found = false;
    for (int i = 0; i < combo->count(); ++i) {
        if (combo->itemData(i).toLongLong() == 42) {
            found = true;
            EXPECT_EQ(combo->itemText(i), QStringLiteral("Work"));
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(EditorPanelTest, CloseSetsNotOpen) {
    panel_.OpenForCreate();
    ASSERT_TRUE(panel_.IsOpen());
    panel_.Close();
    EXPECT_FALSE(panel_.IsOpen());
}

}  // namespace
