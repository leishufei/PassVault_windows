#include <gtest/gtest.h>

#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QFontInfo>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QStackedWidget>
#include <QTextEdit>
#include <QToolButton>

#include "model/category.h"
#include "model/password_entry.h"
#include "ui/editor_panel.h"
#include "ui/theme_manager.h"

namespace {

using passvault::ui::EditorPanel;

QByteArray IconPixels(const QIcon& icon) {
    const QImage image = icon.pixmap(32, 32).toImage().convertToFormat(
        QImage::Format_ARGB32);
    return QCryptographicHash::hash(
        QByteArray(reinterpret_cast<const char*>(image.constBits()),
                   static_cast<qsizetype>(image.sizeInBytes())),
        QCryptographicHash::Sha256);
}

class ThemeGuard {
 public:
    ThemeGuard() : previous_(passvault::ui::ThemeManager::Instance()->theme()) {}
    ~ThemeGuard() {
        passvault::ui::ThemeManager::Instance()->ApplyTheme(previous_);
    }

 private:
    passvault::ui::Theme previous_;
};

void ExpectCenteredTextFits(const QWidget* widget, const QString& text) {
    const QFontMetrics metrics(widget->font());
    const QRect rect = widget->contentsRect();
    const QRect glyphs = metrics.tightBoundingRect(text);
    const int baseline = rect.top() + (rect.height() - metrics.height()) / 2 +
                         metrics.ascent();
    EXPECT_GE(baseline + glyphs.top(), rect.top());
    EXPECT_LE(baseline + glyphs.bottom(), rect.bottom());
    EXPECT_LE(metrics.horizontalAdvance(text), rect.width());
}

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
    QLabel* titleError() {
        return panel_.findChild<QLabel*>(QStringLiteral("EditorTitleError"));
    }
    QLabel* credentialsError() {
        return panel_.findChild<QLabel*>(
            QStringLiteral("EditorCredentialsError"));
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
    EXPECT_EQ(headerTitle()->text(), QStringLiteral("编辑密码 · GitHub"));
    EXPECT_EQ(saveButton()->text(), QStringLiteral("保存更改"));
}

TEST_F(EditorPanelTest, CreateModeUsesFigmaActionText) {
    panel_.OpenForCreate();
    EXPECT_EQ(saveButton()->text(), QStringLiteral("创建密码"));
}

TEST_F(EditorPanelTest, PasswordInputUsesMonoTypography) {
    QWidget parent;
    parent.resize(1200, 800);
    EditorPanel panel(&parent);
    panel.OpenForCreate();
    parent.show();
    QApplication::processEvents();

    auto* password =
        panel.findChild<QLineEdit*>(QStringLiteral("EditorPasswordInput"));
    ASSERT_NE(password, nullptr);
    password->ensurePolished();
    QApplication::processEvents();

    const QFontInfo password_font_info(password->font());
    EXPECT_EQ(password_font_info.family(), QStringLiteral("DM Mono"));
    EXPECT_EQ(password_font_info.pixelSize(), 12);
}

TEST_F(EditorPanelTest, PasswordTypographyBaselineFitsWithoutClipping) {
    panel_.OpenForCreate();
    passwordInput()->setText(QStringLiteral("Dawn!Harbor_2026"));
    panel_.show();
    QApplication::processEvents();

    ExpectCenteredTextFits(passwordInput(), passwordInput()->text());
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
    EXPECT_TRUE(titleInput()->property("error").toBool());
    EXPECT_FALSE(titleError()->isHidden());
    EXPECT_TRUE(credentialsError()->isHidden());
}

TEST_F(EditorPanelTest, SaveWithEmptyCredentialsShowsVisibleError) {
    panel_.OpenForCreate();
    titleInput()->setText(QStringLiteral("Title"));
    QSignalSpy spy(&panel_, &EditorPanel::SaveRequested);
    saveButton()->click();
    EXPECT_EQ(spy.count(), 0);
    EXPECT_TRUE(usernameInput()->property("error").toBool());
    EXPECT_FALSE(credentialsError()->isHidden());
    EXPECT_TRUE(titleError()->isHidden());
}

TEST_F(EditorPanelTest, CancelButtonEmitsSignal) {
    panel_.OpenForCreate();
    QSignalSpy spy(&panel_, &EditorPanel::CancelRequested);
    cancelButton()->click();
    EXPECT_EQ(spy.count(), 1);
}

TEST_F(EditorPanelTest, GenerateButtonOpensEmbeddedGenerator) {
    panel_.OpenForCreate();
    auto* pages =
        panel_.findChild<QStackedWidget*>(QStringLiteral("EditorPages"));
    auto* generator =
        panel_.findChild<QWidget*>(QStringLiteral("EditorGeneratorPage"));
    ASSERT_NE(pages, nullptr);
    ASSERT_NE(generator, nullptr);
    generateButton()->click();
    EXPECT_EQ(pages->currentWidget(), generator);
}

TEST_F(EditorPanelTest, GeneratorApplyFollowsPreviewAvailability) {
    panel_.OpenForCreate();
    generateButton()->click();

    auto* preview = panel_.findChild<QLineEdit*>(
        QStringLiteral("EditorGeneratorPreview"));
    auto* apply = panel_.findChild<QPushButton*>(
        QStringLiteral("EditorGeneratorApply"));
    auto* error = panel_.findChild<QLabel*>(
        QStringLiteral("EditorGeneratorError"));
    const auto toggles = {
        panel_.findChild<QCheckBox*>(
            QStringLiteral("EditorGeneratorUppercase")),
        panel_.findChild<QCheckBox*>(
            QStringLiteral("EditorGeneratorLowercase")),
        panel_.findChild<QCheckBox*>(
            QStringLiteral("EditorGeneratorNumbers")),
        panel_.findChild<QCheckBox*>(
            QStringLiteral("EditorGeneratorSymbols")),
    };

    ASSERT_NE(preview, nullptr);
    ASSERT_NE(apply, nullptr);
    ASSERT_NE(error, nullptr);
    for (auto* toggle : toggles) {
        ASSERT_NE(toggle, nullptr);
        toggle->setChecked(false);
    }

    EXPECT_TRUE(preview->text().isEmpty());
    EXPECT_FALSE(error->isHidden());
    EXPECT_FALSE(apply->isEnabled());

    (*toggles.begin())->setChecked(true);
    EXPECT_FALSE(preview->text().isEmpty());
    EXPECT_TRUE(error->isHidden());
    EXPECT_TRUE(apply->isEnabled());
}

TEST_F(EditorPanelTest, ApplyGeneratedPasswordSetsField) {
    panel_.OpenForCreate();
    panel_.ApplyGeneratedPassword(QStringLiteral("Xy9!mkQ2pL"));
    EXPECT_EQ(passwordInput()->text(), QStringLiteral("Xy9!mkQ2pL"));
}

TEST_F(EditorPanelTest, PasswordChangeUpdatesStrengthLabel) {
    panel_.OpenForCreate();
    passwordInput()->setText(QStringLiteral("A1b!c9D#eF2xY"));
    EXPECT_EQ(strengthLabel()->text(), QStringLiteral("强"));
}

TEST_F(EditorPanelTest, MatchesFigmaDrawerSkeleton) {
    QWidget parent;
    parent.resize(1200, 800);
    EditorPanel panel(&parent);
    panel.OpenForCreate();
    parent.show();
    QApplication::processEvents();

    auto* header = panel.findChild<QWidget*>(QStringLiteral("EditorHeader"));
    auto* back =
        panel.findChild<QToolButton*>(QStringLiteral("EditorHeaderBack"));
    auto* body = panel.findChild<QWidget*>(QStringLiteral("EditorBody"));
    auto* content = panel.findChild<QWidget*>(QStringLiteral("EditorContent"));
    auto* notes =
        panel.findChild<QTextEdit*>(QStringLiteral("EditorNotesInput"));
    auto* footer = panel.findChild<QWidget*>(QStringLiteral("EditorFooter"));
    auto* footer_layout =
        qobject_cast<QBoxLayout*>(footer ? footer->layout() : nullptr);
    auto* header_layout =
        qobject_cast<QBoxLayout*>(header ? header->layout() : nullptr);

    ASSERT_NE(header, nullptr);
    ASSERT_NE(back, nullptr);
    ASSERT_NE(body, nullptr);
    ASSERT_NE(content, nullptr);
    ASSERT_NE(notes, nullptr);
    ASSERT_NE(footer_layout, nullptr);
    ASSERT_NE(header_layout, nullptr);
    EXPECT_EQ(panel.width(), 400);
    EXPECT_EQ(header->height(), 68);
    EXPECT_EQ(panel.findChild<QWidget*>(QStringLiteral("EditorNavigation")),
              nullptr);
    EXPECT_EQ(back->iconSize(), QSize(18, 18));
    EXPECT_EQ(header_layout->contentsMargins(), QMargins(28, 0, 28, 0));
    EXPECT_EQ(content->layout()->contentsMargins(), QMargins(28, 24, 28, 24));
    EXPECT_EQ(body->width(), 340);
    EXPECT_EQ(notes->height(), 96);
    EXPECT_EQ(footer_layout->stretch(0), 1);
    EXPECT_EQ(footer_layout->stretch(1), 0);
    EXPECT_EQ(footer_layout->stretch(2), 0);
    EXPECT_EQ(footer_layout->contentsMargins(), QMargins(28, 16, 28, 16));
}

TEST_F(EditorPanelTest, EmbeddedGeneratorMatchesFigmaFlow) {
    QWidget parent;
    parent.resize(1200, 800);
    EditorPanel panel(&parent);
    panel.OpenForCreate();
    parent.show();
    QApplication::processEvents();

    auto* panel_generate = panel.findChild<QToolButton*>(
        QStringLiteral("EditorGenerateButton"));
    ASSERT_NE(panel_generate, nullptr);
    panel_generate->click();
    QApplication::processEvents();

    auto* pages =
        panel.findChild<QStackedWidget*>(QStringLiteral("EditorPages"));
    auto* generator =
        panel.findChild<QWidget*>(QStringLiteral("EditorGeneratorPage"));
    auto* body =
        panel.findChild<QWidget*>(QStringLiteral("EditorGeneratorBody"));
    auto* slider = panel.findChild<QSlider*>(
        QStringLiteral("EditorGeneratorLengthSlider"));
    auto* preview = panel.findChild<QLineEdit*>(
        QStringLiteral("EditorGeneratorPreview"));
    auto* apply = panel.findChild<QPushButton*>(
        QStringLiteral("EditorGeneratorApply"));
    auto* back = panel.findChild<QToolButton*>(
        QStringLiteral("EditorGeneratorBack"));

    ASSERT_NE(pages, nullptr);
    ASSERT_NE(generator, nullptr);
    ASSERT_NE(body, nullptr);
    ASSERT_NE(slider, nullptr);
    ASSERT_NE(preview, nullptr);
    ASSERT_NE(apply, nullptr);
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(pages->currentWidget(), generator);
    EXPECT_EQ(slider->minimum(), 8);
    EXPECT_EQ(slider->maximum(), 32);
    EXPECT_EQ(body->width(), 340);
    EXPECT_FALSE(preview->text().isEmpty());

    const QString generated = preview->text();
    apply->click();
    EXPECT_EQ(panel.findChild<QLineEdit*>(QStringLiteral("EditorPasswordInput"))
                  ->text(),
              generated);
    EXPECT_EQ(pages->currentWidget(),
              panel.findChild<QWidget*>(QStringLiteral("EditorPage")));

    panel_generate->click();
    back->click();
    EXPECT_TRUE(panel.IsOpen());
    EXPECT_EQ(pages->currentWidget(),
              panel.findChild<QWidget*>(QStringLiteral("EditorPage")));
}

TEST_F(EditorPanelTest, FieldsFollowFigmaOrder) {
    auto* body = panel_.findChild<QWidget*>(QStringLiteral("EditorBody"));
    ASSERT_NE(body, nullptr);

    const auto layout_index = [body](QWidget* widget) {
        while (widget->parentWidget() != body) {
            widget = widget->parentWidget();
        }
        return body->layout()->indexOf(widget);
    };
    EXPECT_LT(layout_index(titleInput()), layout_index(usernameInput()));
    EXPECT_LT(layout_index(usernameInput()), layout_index(passwordInput()));
    EXPECT_LT(layout_index(passwordInput()), layout_index(websiteInput()));
    EXPECT_LT(layout_index(websiteInput()), layout_index(categoryCombo()));
    EXPECT_LT(layout_index(categoryCombo()), layout_index(notesInput()));
}

TEST_F(EditorPanelTest, PreviewToggleSwitchesEchoMode) {
    panel_.OpenForCreate();
    EXPECT_EQ(passwordInput()->echoMode(), QLineEdit::Password);
    previewToggle()->setChecked(true);
    EXPECT_EQ(passwordInput()->echoMode(), QLineEdit::Normal);
    previewToggle()->setChecked(false);
    EXPECT_EQ(passwordInput()->echoMode(), QLineEdit::Password);
}

TEST(EditorPanelThemeTest, ThemeRefreshPreservesEditStateAndPreview) {
    ThemeGuard guard;
    auto* theme = passvault::ui::ThemeManager::Instance();
    theme->ApplyTheme(passvault::ui::Theme::kLight);
    EditorPanel panel;
    panel.OpenForCreate();
    auto* title = panel.findChild<QLineEdit*>(
        QStringLiteral("EditorTitleInput"));
    auto* password = panel.findChild<QLineEdit*>(
        QStringLiteral("EditorPasswordInput"));
    auto* toggle = panel.findChild<QToolButton*>(
        QStringLiteral("EditorPreviewToggle"));
    auto* pages = panel.findChild<QStackedWidget*>(QStringLiteral("EditorPages"));
    ASSERT_NE(title, nullptr);
    ASSERT_NE(password, nullptr);
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(pages, nullptr);
    title->setText(QStringLiteral("Draft title"));
    password->setText(QStringLiteral("draft-secret"));
    toggle->setChecked(true);
    const QByteArray light_icon = IconPixels(toggle->icon());
    auto* shadow = qobject_cast<QGraphicsDropShadowEffect*>(
        panel.graphicsEffect());
    ASSERT_NE(shadow, nullptr);
    const QColor light_shadow = shadow->color();
    QWidget* current_page = pages->currentWidget();

    theme->ApplyTheme(passvault::ui::Theme::kDark);

    EXPECT_NE(IconPixels(toggle->icon()), light_icon);
    EXPECT_NE(shadow->color(), light_shadow);
    EXPECT_EQ(pages->currentWidget(), current_page);
    EXPECT_EQ(title->text(), QStringLiteral("Draft title"));
    EXPECT_EQ(password->text(), QStringLiteral("draft-secret"));
    EXPECT_EQ(password->echoMode(), QLineEdit::Normal);
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
