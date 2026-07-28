#include <gtest/gtest.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QString>

#include "ui/generator_dialog.h"

namespace {

using passvault::ui::GeneratorDialog;

class GeneratorDialogTest : public ::testing::Test {
 protected:
    GeneratorDialog dlg_;

    QSlider* slider() {
        return dlg_.findChild<QSlider*>(QStringLiteral("GeneratorLengthSlider"));
    }
    QSpinBox* spin() {
        return dlg_.findChild<QSpinBox*>(QStringLiteral("GeneratorLengthSpin"));
    }
    QLineEdit* preview() { return dlg_.findChild<QLineEdit*>(); }
    QLabel* error() {
        return dlg_.findChild<QLabel*>(QStringLiteral("FormError"));
    }
    QProgressBar* strength() {
        return dlg_.findChild<QProgressBar*>(QStringLiteral("StrengthBar"));
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
    QPushButton* acceptButton() {
        auto* box = dlg_.findChild<QDialogButtonBox*>();
        if (!box) return nullptr;
        for (auto* b : box->buttons()) {
            if (box->buttonRole(b) == QDialogButtonBox::AcceptRole)
                return qobject_cast<QPushButton*>(b);
        }
        return nullptr;
    }
};

TEST_F(GeneratorDialogTest, DefaultGeneratesPasswordOfDefaultLength) {
    ASSERT_NE(preview(), nullptr);
    EXPECT_EQ(preview()->text().length(), 16);
}

TEST_F(GeneratorDialogTest, SliderAndSpinStayInSync) {
    ASSERT_NE(slider(), nullptr);
    ASSERT_NE(spin(), nullptr);
    slider()->setValue(24);
    EXPECT_EQ(spin()->value(), 24);
    spin()->setValue(12);
    EXPECT_EQ(slider()->value(), 12);
}

TEST_F(GeneratorDialogTest, ChangingLengthChangesPasswordLength) {
    slider()->setValue(32);
    EXPECT_EQ(preview()->text().length(), 32);
}

TEST_F(GeneratorDialogTest, UncheckingAllCharTypesShowsErrorAndClearsPreview) {
    upper()->setChecked(false);
    lower()->setChecked(false);
    number()->setChecked(false);
    symbol()->setChecked(false);
    EXPECT_TRUE(preview()->text().isEmpty());
    ASSERT_NE(error(), nullptr);
    EXPECT_FALSE(error()->text().isEmpty());
}

TEST_F(GeneratorDialogTest, AcceptStoresPreviewAsPassword) {
    const QString pw = preview()->text();
    ASSERT_FALSE(pw.isEmpty());
    auto* ok = acceptButton();
    ASSERT_NE(ok, nullptr);
    QSignalSpy accepted(&dlg_, &QDialog::accepted);
    ok->click();
    EXPECT_EQ(dlg_.password(), pw);
    EXPECT_EQ(accepted.count(), 1);
}

TEST_F(GeneratorDialogTest, StrengthBarReflectsGeneratedPassword) {
    ASSERT_NE(strength(), nullptr);
    EXPECT_GT(strength()->value(), 0);
}

}  // namespace
