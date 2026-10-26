#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QSignalSpy>

#include "ui/clipboard_manager.h"

namespace {

TEST(ClipboardManagerTest, UsesConfiguredTimeoutAndPreservesUnrelatedClipboard) {
  auto* manager = passvault::ui::ClipboardManager::Instance();
  const int original_timeout = manager->timeout_ms();
  manager->set_default_timeout_ms(47000);
  QSignalSpy scheduled(manager, &passvault::ui::ClipboardManager::ClearScheduled);
  manager->CopySensitive(QStringLiteral("fixture-value"));
  ASSERT_EQ(scheduled.count(), 1);
  EXPECT_EQ(scheduled.front().front().toInt(), 47000);
  QApplication::clipboard()->setText(QStringLiteral("unrelated"));
  manager->ClearSensitive();
  EXPECT_EQ(QApplication::clipboard()->text(), QStringLiteral("unrelated"));
  manager->CopySensitive(QStringLiteral("fixture-value"));
  manager->ClearSensitive();
  EXPECT_TRUE(QApplication::clipboard()->text().isEmpty());
  manager->set_default_timeout_ms(original_timeout);
}

}  // namespace
