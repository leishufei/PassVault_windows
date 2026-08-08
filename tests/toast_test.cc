#include <gtest/gtest.h>

#include <QFrame>
#include <QLabel>
#include <QPointer>
#include <QString>
#include <QTest>
#include <QWidget>

#include "ui/toast.h"
#include "ui/theme_manager.h"

namespace {

using passvault::ui::Toast;

TEST(ToastTest, ShowDisplaysTextForInfoLevel) {
    QWidget parent;
    parent.resize(400, 300);

    Toast::Show(&parent, QStringLiteral("hello"), Toast::Level::kInfo, 2400);

    auto* toast = parent.findChild<QFrame*>(QStringLiteral("Toast"));
    ASSERT_NE(toast, nullptr);
    auto* label = toast->findChild<QLabel*>(QStringLiteral("ToastText"));
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->text(), QStringLiteral("hello"));
}

TEST(ToastTest, LevelSelectsObjectName) {
    QWidget parent;
    parent.resize(400, 300);

    Toast::Show(&parent, QStringLiteral("ok"), Toast::Level::kSuccess, 2400);
    EXPECT_NE(parent.findChild<QFrame*>(QStringLiteral("ToastSuccess")), nullptr);

    Toast::Show(&parent, QStringLiteral("bad"), Toast::Level::kError, 2400);
    EXPECT_NE(parent.findChild<QFrame*>(QStringLiteral("ToastError")), nullptr);

    Toast::Show(&parent, QStringLiteral("warn"), Toast::Level::kWarning, 2400);
    EXPECT_NE(parent.findChild<QFrame*>(QStringLiteral("ToastWarning")), nullptr);
}

TEST(ToastTest, StyleSheetCoversAllToastLevels) {
    const QString css =
        passvault::ui::ThemeManager::Instance()->LoadStyleSheet();
    EXPECT_NE(css.indexOf(QStringLiteral("#ToastSuccess")), -1);
    EXPECT_NE(css.indexOf(QStringLiteral("#ToastWarning")), -1);
    EXPECT_NE(css.indexOf(QStringLiteral("#ToastError")), -1);
}

TEST(ToastTest, AutoDismissDestroysToast) {
    QWidget parent;
    parent.resize(400, 300);

    Toast::Show(&parent, QStringLiteral("bye"), Toast::Level::kInfo, 50);
    QPointer<QFrame> toast = parent.findChild<QFrame*>(QStringLiteral("Toast"));
    ASSERT_FALSE(toast.isNull());

    QTest::qWait(50 + 200 + 250);  // dismiss delay + fade-out + buffer
    EXPECT_TRUE(toast.isNull());
}

}  // namespace
