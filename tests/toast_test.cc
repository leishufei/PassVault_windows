#include <gtest/gtest.h>

#include <QFrame>
#include <QApplication>
#include <QEvent>
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

TEST(ToastTest, NewMessageReplacesPreviousToast) {
    QWidget parent;
    parent.resize(400, 300);
    Toast::Show(&parent, QStringLiteral("first"));
    QPointer<Toast> first = parent.findChild<Toast*>();
    Toast::Show(&parent, QStringLiteral("latest"), Toast::Level::kWarning);
    EXPECT_TRUE(first.isNull() || first->isHidden());
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto toasts = parent.findChildren<Toast*>();
    ASSERT_EQ(toasts.size(), 1);
    EXPECT_EQ(toasts[0]->findChild<QLabel*>(QStringLiteral("ToastText"))->text(),
              QStringLiteral("latest"));
}

TEST(ToastTest, ParentResizeKeepsToastCenteredAndInside) {
    QWidget parent;
    parent.resize(600, 300);
    parent.show();
    Toast::Show(&parent, QStringLiteral("message"));
    auto* toast = parent.findChild<Toast*>();
    ASSERT_NE(toast, nullptr);
    parent.resize(240, 160);
    QApplication::processEvents();
    EXPECT_TRUE(parent.rect().contains(toast->geometry()));
    EXPECT_LE(qAbs(toast->geometry().center().x() - parent.rect().center().x()), 1);
    EXPECT_EQ(parent.height() - toast->geometry().bottom() - 1, 32);
}

TEST(ToastTest, LongTextWrapsAtAvailableWidth) {
    QWidget parent;
    parent.resize(320, 300);
    parent.show();
    Toast::Show(&parent, QString(160, QLatin1Char('w')));
    auto* toast = parent.findChild<Toast*>();
    ASSERT_NE(toast, nullptr);
    QApplication::processEvents();
    auto* label = toast->findChild<QLabel*>(QStringLiteral("ToastText"));
    EXPECT_LE(toast->width(), parent.width() - 40);
    EXPECT_TRUE(parent.rect().contains(toast->geometry()));
    EXPECT_GE(label->height(), label->heightForWidth(label->width()));
}

TEST(ToastTest, MessagesAreRenderedAsPlainText) {
    QWidget parent;
    parent.resize(400, 300);
    const QString text = QStringLiteral("<b>literal message</b>");
    Toast::Show(&parent, text);
    auto* label = parent.findChild<QLabel*>(QStringLiteral("ToastText"));
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->textFormat(), Qt::PlainText);
    EXPECT_EQ(label->text(), text);
}

}  // namespace
