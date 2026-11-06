#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QString>

#include "ui/clipboard_manager.h"
#include "ui/generator_dialog.h"

namespace {

using passvault::ui::GeneratorDialog;

class GeneratorDialogTest : public ::testing::Test {
 protected:
    GeneratorDialog dlg_;

    QSlider* slider() {
        return dlg_.findChild<QSlider*>(QStringLiteral("GeneratorLengthSlider"));
    }
    QLabel* lengthValue() {
        return dlg_.findChild<QLabel*>(QStringLiteral("GeneratorLengthValue"));
    }
    QLineEdit* preview() {
        return dlg_.findChild<QLineEdit*>(QStringLiteral("GeneratorPreview"));
    }
    QLabel* error() {
        return dlg_.findChild<QLabel*>(QStringLiteral("FormError"));
    }
    QCheckBox* upper() {
        return dlg_.findChild<QCheckBox*>(QStringLiteral("GeneratorUpper"));
    }
    QCheckBox* lower() {
        return dlg_.findChild<QCheckBox*>(QStringLiteral("GeneratorLower"));
    }
    QCheckBox* number() {
        return dlg_.findChild<QCheckBox*>(QStringLiteral("GeneratorNumber"));
    }
    QCheckBox* symbol() {
        return dlg_.findChild<QCheckBox*>(QStringLiteral("GeneratorSymbol"));
    }
    QPushButton* copyButton() {
        return dlg_.findChild<QPushButton*>(QStringLiteral("GeneratorCopyButton"));
    }
    int filledSegments() {
        int filled = 0;
        for (auto* frame : dlg_.findChildren<QFrame*>()) {
            if (frame->property("strength-segment").toBool() &&
                frame->property("filled").toBool()) {
                ++filled;
            }
        }
        return filled;
    }
};

TEST_F(GeneratorDialogTest, DefaultGeneratesPasswordOfDefaultLength) {
    ASSERT_NE(preview(), nullptr);
    EXPECT_EQ(preview()->text().length(), GeneratorDialog::kDefaultLength);
    EXPECT_TRUE(preview()->isReadOnly());
}

TEST_F(GeneratorDialogTest, LengthRangeMatchesEditorGenerator) {
    ASSERT_NE(slider(), nullptr);
    EXPECT_EQ(slider()->minimum(), 8);
    EXPECT_EQ(slider()->maximum(), 32);
}

TEST_F(GeneratorDialogTest, ChangingLengthChangesPasswordLengthAndLabel) {
    slider()->setValue(32);
    EXPECT_EQ(preview()->text().length(), 32);
    ASSERT_NE(lengthValue(), nullptr);
    EXPECT_EQ(lengthValue()->text(), QStringLiteral("32"));
}

TEST_F(GeneratorDialogTest, UncheckingAllCharTypesShowsErrorAndDisablesCopy) {
    upper()->setChecked(false);
    lower()->setChecked(false);
    number()->setChecked(false);
    symbol()->setChecked(false);
    EXPECT_TRUE(preview()->text().isEmpty());
    ASSERT_NE(error(), nullptr);
    EXPECT_FALSE(error()->text().isEmpty());
    ASSERT_NE(copyButton(), nullptr);
    EXPECT_FALSE(copyButton()->isEnabled());
    EXPECT_EQ(filledSegments(), 0);
}

TEST_F(GeneratorDialogTest, CopyUsesClipboardAndKeepsDialogOpen) {
    const QString pw = preview()->text();
    ASSERT_FALSE(pw.isEmpty());
    QSignalSpy accepted(&dlg_, &QDialog::accepted);
    QSignalSpy rejected(&dlg_, &QDialog::rejected);
    ASSERT_NE(copyButton(), nullptr);
    copyButton()->click();
    EXPECT_EQ(QApplication::clipboard()->text(), pw);
    EXPECT_EQ(dlg_.password(), pw);
    EXPECT_EQ(accepted.count(), 0);
    EXPECT_EQ(rejected.count(), 0);
    passvault::ui::ClipboardManager::Instance()->ClearSensitive();
}

TEST_F(GeneratorDialogTest, CloseButtonRejects) {
    QSignalSpy rejected(&dlg_, &QDialog::rejected);
    auto* close = dlg_.button(QMessageBox::Close);
    ASSERT_NE(close, nullptr);
    close->click();
    EXPECT_EQ(rejected.count(), 1);
}

TEST_F(GeneratorDialogTest, StrengthMeterReflectsGeneratedPassword) {
    EXPECT_GT(filledSegments(), 0);
}

}  // namespace
