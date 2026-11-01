#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontInfo>
#include <QGlyphRun>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTest>
#include <QTextLayout>
#include <QTimer>
#include <QWindow>

#include "crypto/crypto_service.h"
#include "crypto/secure_bytes.h"
#include "crypto/session_key.h"
#include "storage/category_dao.h"
#include "storage/database.h"
#include "storage/password_dao.h"
#include "storage/schema.h"
#include "ui/detail_panel.h"
#include "ui/editor_panel.h"
#include "ui/generator_dialog.h"
#include "ui/import_preview_dialog.h"
#include "ui/main_window.h"
#include "ui/master_password_dialog.h"
#include "ui/oauth_wizard_dialog.h"
#include "ui/preferences_page.h"
#include "ui/theme_manager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {

namespace ui = passvault::ui;

void AuditPage(QWidget* page, const QString& name, QJsonArray* measurements) {
  page->show();
  QTest::qWait(100);
#ifdef Q_OS_WIN
  if (qobject_cast<QDialog*>(page) &&
      QApplication::platformName() == QStringLiteral("windows")) {
    BOOL rendering = TRUE;
    EXPECT_TRUE(SUCCEEDED(DwmGetWindowAttribute(
        reinterpret_cast<HWND>(page->winId()), DWMWA_NCRENDERING_ENABLED,
        &rendering, sizeof(rendering))));
    EXPECT_FALSE(rendering) << name.toStdString();
  }
#endif
  const QString capture = qEnvironmentVariable("PASSVAULT_UI_CAPTURE_DIR");
  if (!capture.isEmpty()) {
    EXPECT_TRUE(page->grab().save(QDir(capture).filePath(name + ".png")));
  }
  for (auto* widget : page->findChildren<QWidget*>()) {
    if (!widget->isVisible() ||
        (!qobject_cast<QLabel*>(widget) &&
         !qobject_cast<QAbstractButton*>(widget) &&
         !qobject_cast<QAbstractItemView*>(widget) &&
         !qobject_cast<QLineEdit*>(widget))) {
      continue;
    }
    const QFont font = widget->font();
    const QFontInfo info(font);
    QJsonObject record{{"page", name},
                       {"object", widget->objectName()},
                       {"class", widget->metaObject()->className()},
                       {"family", info.family()},
                       {"size", info.pixelSize()},
                       {"weight", static_cast<int>(info.weight())}};
    const bool monospace = info.family() == QStringLiteral("Consolas");
    QTextLayout glyphs(monospace ? QStringLiteral("PassVault0123")
                                 : QStringLiteral("PassVault 密码设置"), font);
    glyphs.beginLayout();
    glyphs.createLine();
    glyphs.endLayout();
    QJsonArray families;
    QJsonArray glyph_weights;
    for (const auto& run : glyphs.glyphRuns()) {
      const QString family = run.rawFont().familyName();
      families.append(family);
      glyph_weights.append(run.rawFont().weight());
      EXPECT_FALSE(family.contains(QStringLiteral("Times")) ||
                   family.contains(QStringLiteral("SimSun")))
          << name.toStdString() << '/' << widget->objectName().toStdString();
    }
    record.insert("glyph_families", families);
    record.insert("glyph_weights", glyph_weights);
    measurements->append(record);

    const QString object = widget->objectName();
    if (qobject_cast<QPushButton*>(widget) &&
        widget->property("accent").toBool()) {
      EXPECT_EQ(info.weight(), QFont::DemiBold) << object.toStdString();
    }
    if (object == "ListTitle" || object == "DetailTitle" ||
        object == "PreferencesHeaderTitle" || object == "DialogTitle" ||
        object == "EditorHeaderTitle" ||
        object == "EditorGeneratorHeaderTitle") {
      EXPECT_EQ(info.pixelSize(), 16) << object.toStdString();
      EXPECT_EQ(info.weight(), QFont::DemiBold) << object.toStdString();
    }
    if (object == "SettingSectionTitle" || object == "SyncHeadingTitle" ||
        object == "SecurityPageTitle") {
      EXPECT_EQ(info.pixelSize(), 15) << object.toStdString();
      EXPECT_EQ(info.weight(), QFont::Normal) << object.toStdString();
    }
    if (object == "EditorSectionTitle") {
      EXPECT_EQ(info.pixelSize(), 15) << object.toStdString();
      EXPECT_EQ(info.weight(), QFont::DemiBold) << object.toStdString();
    }
    if (object == "SettingRowTitle" || object == "SyncAuthTitle" ||
        object == "SyncAccountTitle") {
      EXPECT_EQ(info.pixelSize(), 14) << object.toStdString();
      EXPECT_EQ(info.weight(), QFont::Normal) << object.toStdString();
    }
    if (object == "SettingRowDesc" || object == "SyncAuthDesc" ||
        object == "SyncHeadingDesc" || object == "SecurityPageDescription" ||
        object == "SyncAccountEmail" || object == "SyncLastTitle" ||
        object == "SyncLastValue" || object == "SyncStateText" ||
        object == "DetailStampValue") {
      EXPECT_EQ(info.pixelSize(), 12) << object.toStdString();
      EXPECT_EQ(info.weight(), QFont::Normal) << object.toStdString();
    }
  }
}

TEST(UiTypographyTest, EveryPageUsesSharedTypeHierarchyAndSansSerifGlyphs) {
  auto database = passvault::storage::Database::OpenInMemory();
  ASSERT_TRUE(database);
  passvault::storage::EnsureCurrentSchema(*database);
  passvault::storage::PasswordDao passwords(*database);
  passvault::storage::CategoryDao categories(*database);
  const QByteArray sample_key(32, '\x01');
  auto key = passvault::crypto::SessionKey::FromSecureBytes(
      passvault::crypto::SecureBytes(
          reinterpret_cast<const std::uint8_t*>(sample_key.constData()),
          sample_key.size()));
  ASSERT_TRUE(key);
  passvault::model::PasswordEntry entry;
  entry.uuid = QStringLiteral("visual-audit-entry");
  entry.title = QStringLiteral("演示账号");
  entry.username = QStringLiteral("demo@example.com");
  entry.website = QStringLiteral("https://example.com");
  entry.notes = QStringLiteral("仅用于界面检查的虚构数据");
  entry.created_at = entry.updated_at = 1793491200000LL;
  entry.password_iv = QByteArray(12, '\x02');
  const QByteArray sample = QByteArrayLiteral("VisualDemoOnly");
  entry.encrypted_password = passvault::crypto::CryptoService::EncryptGcm(
      key->data(), key->size(),
      reinterpret_cast<const std::uint8_t*>(entry.password_iv.constData()),
      entry.password_iv.size(),
      reinterpret_cast<const std::uint8_t*>(sample.constData()), sample.size());
  ASSERT_TRUE(passwords.Insert(entry));
  ui::MainWindow window({&passwords, &categories, &*key, nullptr, nullptr});
  window.resize(1200, 800);
  auto* preferences = window.preferences_page();
  auto* pages = qobject_cast<QStackedWidget*>(preferences->parentWidget());
  ASSERT_NE(pages, nullptr);
  auto* theme = ui::ThemeManager::Instance();
  const auto previous = theme->theme();
  QJsonArray measurements;
  for (auto value : {ui::Theme::kLight, ui::Theme::kDark}) {
    theme->ApplyTheme(value);
    pages->setCurrentIndex(0);
    const QString suffix = value == ui::Theme::kLight ? "-light" : "-dark";
    AuditPage(&window, "list" + suffix, &measurements);
    auto* sections = window.findChild<QListWidget*>("SidebarSectionList");
    ASSERT_NE(sections, nullptr);
    const QStringList section_names{"favorites", "uncategorized", "trash"};
    for (int index = 1; index < 4; ++index) {
      sections->setCurrentRow(index);
      AuditPage(&window, section_names[index - 1] + suffix, &measurements);
    }
    sections->setCurrentRow(0);
    auto* list = window.findChild<QListWidget*>("PasswordList");
    ASSERT_NE(list, nullptr);
    list->setCurrentRow(0);
    AuditPage(&window, "detail" + suffix, &measurements);
    auto* editor = window.findChild<ui::EditorPanel*>();
    ASSERT_NE(editor, nullptr);
    editor->OpenForCreate();
    QTest::qWait(250);
    AuditPage(&window, "editor" + suffix, &measurements);
    QMetaObject::invokeMethod(editor, "OnShowGenerator");
    AuditPage(&window, "editor-generator" + suffix, &measurements);
    editor->ClearSensitiveState();
    pages->setCurrentWidget(preferences);
    auto* tabs = preferences->findChild<QTabWidget*>("PreferencesTabs");
    ASSERT_NE(tabs, nullptr);
    preferences->show();
    for (int index = 0; index < 3; ++index) {
      tabs->setCurrentIndex(index);
      AuditPage(preferences, "settings-" + QString::number(index) + suffix,
                &measurements);
    }
    preferences->SetSyncState(ui::PreferencesPage::SyncUiState::kConnected,
                              QStringLiteral("demo@example.com"));
    tabs->setCurrentIndex(1);
    AuditPage(preferences, "settings-connected" + suffix, &measurements);
    preferences->SetSyncState(ui::PreferencesPage::SyncUiState::kDisconnected,
                              QString());

    ui::OAuthWizardDialog oauth(nullptr, &window);
    AuditPage(&oauth, "oauth" + suffix, &measurements);
    oauth.hide();
    ui::GeneratorDialog generator(&window);
    AuditPage(&generator, "generator" + suffix, &measurements);
    generator.hide();
    ui::ImportPreviewDialog imports({}, &window);
    AuditPage(&imports, "import" + suffix, &measurements);
    imports.hide();
    for (auto mode : {ui::MasterPasswordDialog::Mode::kSetup,
                      ui::MasterPasswordDialog::Mode::kUnlock,
                      ui::MasterPasswordDialog::Mode::kChange}) {
      ui::MasterPasswordDialog master(mode, &window);
      AuditPage(&master,
                "master-" + QString::number(static_cast<int>(mode)) + suffix,
                &measurements);
      master.hide();
    }
    QInputDialog category(&window);
    category.setLabelText(QStringLiteral("分类名称"));
    AuditPage(&category, "category" + suffix, &measurements);
    category.hide();
    QMessageBox confirmation(QMessageBox::Question, QStringLiteral("确认"),
                              QStringLiteral("确定删除演示条目吗？"),
                              QMessageBox::Yes | QMessageBox::No, &window);
    AuditPage(&confirmation, "confirmation" + suffix, &measurements);
    confirmation.hide();
  }
  theme->ApplyTheme(previous);
  const QString capture = qEnvironmentVariable("PASSVAULT_UI_CAPTURE_DIR");
  if (!capture.isEmpty()) {
    QFile report(QDir(capture).filePath("font-audit.json"));
    ASSERT_TRUE(report.open(QIODevice::WriteOnly));
    report.write(QJsonDocument(measurements).toJson());
  }
}

TEST(UiTypographyTest, NativeDialogHasNoSystemBorder) {
  if (QApplication::platformName() != QStringLiteral("windows")) GTEST_SKIP();
  auto database = passvault::storage::Database::OpenInMemory();
  ASSERT_TRUE(database);
  passvault::storage::EnsureCurrentSchema(*database);
  passvault::storage::PasswordDao passwords(*database);
  passvault::storage::CategoryDao categories(*database);
  ui::MainWindow parent({&passwords, &categories, nullptr, nullptr, nullptr});
  parent.setWindowTitle(
      QStringLiteral("PassVault Visual Audit - synthetic data"));
  parent.resize(1200, 800);
  parent.show();
  parent.raise();
  parent.activateWindow();
  QTest::qWait(100);
  auto* theme = ui::ThemeManager::Instance();
  const auto previous = theme->theme();
  for (auto value : {ui::Theme::kLight, ui::Theme::kDark}) {
    theme->ApplyTheme(value);
    QTest::qWait(100);
    ui::OAuthWizardDialog oauth(nullptr, &parent);
    QInputDialog category(&parent);
    category.setLabelText(QStringLiteral("分类名称"));
    category.setTextValue(QStringLiteral("演示分类"));
    QMessageBox remove_category(
        QMessageBox::Question, QStringLiteral("删除分类"),
        QStringLiteral("删除分类“演示分类”及其中的所有密码？密码将移入回收站，"
                       "不会永久删除。"),
        QMessageBox::Yes | QMessageBox::No, &parent);
    QMessageBox remove_entry(
        QMessageBox::Question, QStringLiteral("移入回收站"),
        QStringLiteral("确定将该密码移至回收站吗？"),
        QMessageBox::Yes | QMessageBox::No, &parent);
    const QList<QDialog*> dialogs{&oauth, &category, &remove_category,
                                  &remove_entry};
    const QStringList names{"oauth", "category", "delete-category",
                             "delete-entry"};
    for (int index = 0; index < dialogs.size(); ++index) {
      auto* dialog = dialogs[index];
      const QString name = names[index] +
          (value == ui::Theme::kLight ? "-light" : "-dark");
      SCOPED_TRACE(name.toStdString());
      QTimer::singleShot(50, dialog, [dialog, &parent]() {
        dialog->move(
            parent.mapToGlobal(parent.rect().center() + QPoint(0, 120)) -
            dialog->rect().center());
      });
      QTimer::singleShot(350, dialog, [dialog, &parent, theme, name]() {
        EXPECT_GE(dialog->windowHandle()->format().alphaBufferSize(), 8);
#ifdef Q_OS_WIN
        BOOL rendering = TRUE;
        const HRESULT result = DwmGetWindowAttribute(
            reinterpret_cast<HWND>(dialog->winId()), DWMWA_NCRENDERING_ENABLED,
            &rendering, sizeof(rendering));
        EXPECT_TRUE(SUCCEEDED(result));
        EXPECT_FALSE(rendering);
        EXPECT_NE(GetWindowLongPtr(reinterpret_cast<HWND>(dialog->winId()),
                                   GWL_EXSTYLE) & WS_EX_LAYERED, 0);
#endif
        if (qobject_cast<ui::OAuthWizardDialog*>(dialog)) {
          EXPECT_EQ(dialog->width(), 480);
          EXPECT_LT(dialog->height(), 200);
        }
        const QRect bounds = dialog->frameGeometry().adjusted(-6, -6, 6, 6);
        EXPECT_TRUE(parent.frameGeometry().contains(bounds));
        const auto composed = dialog->screen()->grabWindow(
            0, bounds.x(), bounds.y(), bounds.width(), bounds.height());
        EXPECT_FALSE(composed.isNull());
        if (!composed.isNull()) {
          const QImage pixels = composed.toImage();
          const qreal scale = composed.devicePixelRatio();
          bool primary_visible = false;
          for (auto* button : dialog->findChildren<QPushButton*>()) {
            if (!button->property("accent").toBool()) continue;
            const QPoint sample = button->mapToGlobal(
                QPoint(button->width() / 2, button->height() / 2 - 10)) -
                bounds.topLeft();
            const QColor color = pixels.pixelColor(sample * scale);
            EXPECT_GT(color.blue(), color.red() + 50)
                << name.toStdString() << ' ' << sample.x() << ',' << sample.y()
                << ' ' << color.name().toStdString();
            primary_visible |= color.blue() > color.red() + 50;
          }
          EXPECT_TRUE(primary_visible);
          const QColor border = theme->Color(QStringLiteral("border-panel"));
          const QColor surface = theme->Color(QStringLiteral("bg-canvas"));
          const QImage underlay = parent.grab().toImage();
          int dark_pixels = 0;
          const int padding = qRound(6 * scale);
          for (int y = padding; y < pixels.height() - padding; ++y) {
            for (int x = padding; x < pixels.width() - padding; ++x) {
              const int edge_x = qMin(x - padding,
                                      pixels.width() - padding - 1 - x);
              const int edge_y = qMin(y - padding,
                                      pixels.height() - padding - 1 - y);
              if (edge_x > 4 * scale && edge_y > 4 * scale) continue;
              const QColor color = pixels.pixelColor(x, y);
              const QPoint point = parent.mapFromGlobal(
                  bounds.topLeft() + QPoint(qRound(x / scale),
                                            qRound(y / scale)));
              const QColor background = underlay.pixelColor(
                  point * parent.devicePixelRatioF());
              if (color.red() <
                      qMin(surface.red(),
                           qMin(border.red(), background.red())) - 2 ||
                  color.green() <
                      qMin(surface.green(),
                           qMin(border.green(), background.green())) - 2 ||
                  color.blue() <
                      qMin(surface.blue(),
                           qMin(border.blue(), background.blue())) - 2) {
                ++dark_pixels;
              }
            }
          }
          EXPECT_EQ(dark_pixels, 0);
          const QString capture =
              qEnvironmentVariable("PASSVAULT_UI_CAPTURE_DIR");
          if (!capture.isEmpty()) {
            EXPECT_TRUE(composed.save(QDir(capture).filePath(
                "native-" + name + ".png")));
          }
        }
        dialog->reject();
      });
      dialog->exec();
    }
  }
  theme->ApplyTheme(previous);
}

}  // namespace
