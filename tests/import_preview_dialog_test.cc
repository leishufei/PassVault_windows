#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QLabel>
#include <QStringList>
#include <QTabWidget>
#include <QTreeWidget>

#include "csv/csv_models.h"
#include "ui/import_preview_dialog.h"

namespace {

namespace csv = passvault::csv;
using passvault::ui::ImportPreviewDialog;

// Builds a result with n insert + m update valid rows and k invalid rows.
csv::CsvValidationResult MakeResult(int n_insert, int m_update, int k_invalid) {
    csv::CsvValidationResult r;
    r.format = csv::CsvFormat::kExtended;
    r.total_rows = n_insert + m_update + k_invalid;
    int idx = 1;
    for (int i = 0; i < n_insert; ++i) {
        csv::ValidatedPasswordRow row;
        row.row_index = idx++;
        row.action = csv::ImportAction::kInsert;
        row.title = QStringLiteral("Insert%1").arg(i);
        row.username = QStringLiteral("user%1").arg(i);
        row.category_name = QStringLiteral("Cat");
        r.valid_rows.append(row);
    }
    for (int i = 0; i < m_update; ++i) {
        csv::ValidatedPasswordRow row;
        row.row_index = idx++;
        row.action = csv::ImportAction::kUpdate;
        row.title = QStringLiteral("Update%1").arg(i);
        csv::FieldDiff d;
        d.field = QStringLiteral("password");
        d.old_value = QStringLiteral("old");
        d.new_value = QStringLiteral("new");
        row.diffs.append(d);
        r.valid_rows.append(row);
    }
    for (int i = 0; i < k_invalid; ++i) {
        csv::InvalidRow row;
        row.row_index = idx++;
        row.error_message = QStringLiteral("bad");
        row.raw_data = QStringList{QStringLiteral("a"), QStringLiteral("b")};
        r.invalid_rows.append(row);
    }
    return r;
}

QLabel* Summary(const ImportPreviewDialog& d) {
    return d.findChild<QLabel*>(QStringLiteral("DialogSubtitle"));
}

QTabWidget* Tabs(const ImportPreviewDialog& d) {
    return d.findChild<QTabWidget*>(QStringLiteral("ImportPreviewTabs"));
}

QAbstractButton* RoleButton(const ImportPreviewDialog& d,
                            QDialogButtonBox::ButtonRole role) {
    auto* box = d.findChild<QDialogButtonBox*>();
    if (!box) return nullptr;
    for (auto* b : box->buttons()) {
        if (box->buttonRole(b) == role) return b;
    }
    return nullptr;
}

TEST(ImportPreviewDialogTest, SummaryReflectsCounts) {
    ImportPreviewDialog dlg(MakeResult(3, 2, 1));
    ASSERT_NE(Summary(dlg), nullptr);
    EXPECT_EQ(Summary(dlg)->text(),
              QStringLiteral("共 6 行 · 新增 3 · 更新 2 · 无效 1"));
}

TEST(ImportPreviewDialogTest, TabTitlesShowCounts) {
    ImportPreviewDialog dlg(MakeResult(3, 2, 1));
    auto* tabs = Tabs(dlg);
    ASSERT_NE(tabs, nullptr);
    ASSERT_EQ(tabs->count(), 3);
    EXPECT_EQ(tabs->tabText(0), QStringLiteral("新增 (3)"));
    EXPECT_EQ(tabs->tabText(1), QStringLiteral("更新 (2)"));
    EXPECT_EQ(tabs->tabText(2), QStringLiteral("无效 (1)"));
}

TEST(ImportPreviewDialogTest, TreesPopulatedPerAction) {
    ImportPreviewDialog dlg(MakeResult(3, 2, 1));
    auto* tabs = Tabs(dlg);
    ASSERT_NE(tabs, nullptr);
    auto* insert_tree = qobject_cast<QTreeWidget*>(tabs->widget(0));
    auto* update_tree = qobject_cast<QTreeWidget*>(tabs->widget(1));
    auto* invalid_tree = qobject_cast<QTreeWidget*>(tabs->widget(2));
    ASSERT_NE(insert_tree, nullptr);
    ASSERT_NE(update_tree, nullptr);
    ASSERT_NE(invalid_tree, nullptr);
    EXPECT_EQ(insert_tree->topLevelItemCount(), 3);
    EXPECT_EQ(update_tree->topLevelItemCount(), 2);
    EXPECT_EQ(invalid_tree->topLevelItemCount(), 1);
    // Each update row expands into one child per diff.
    EXPECT_EQ(update_tree->topLevelItem(0)->childCount(), 1);
}

TEST(ImportPreviewDialogTest, ConfirmDisabledWithoutValidRows) {
    ImportPreviewDialog dlg(MakeResult(0, 0, 3));
    auto* confirm = RoleButton(dlg, QDialogButtonBox::AcceptRole);
    ASSERT_NE(confirm, nullptr);
    EXPECT_FALSE(confirm->isEnabled());
}

TEST(ImportPreviewDialogTest, ConfirmEnabledWithValidRows) {
    ImportPreviewDialog dlg(MakeResult(1, 0, 2));
    auto* confirm = RoleButton(dlg, QDialogButtonBox::AcceptRole);
    ASSERT_NE(confirm, nullptr);
    EXPECT_TRUE(confirm->isEnabled());
}

TEST(ImportPreviewDialogTest, ClickConfirmAccepts) {
    ImportPreviewDialog dlg(MakeResult(2, 0, 0));
    RoleButton(dlg, QDialogButtonBox::AcceptRole)->click();
    EXPECT_EQ(dlg.result(), static_cast<int>(QDialog::Accepted));
}

TEST(ImportPreviewDialogTest, ClickCancelRejects) {
    ImportPreviewDialog dlg(MakeResult(2, 1, 1));
    RoleButton(dlg, QDialogButtonBox::RejectRole)->click();
    EXPECT_EQ(dlg.result(), static_cast<int>(QDialog::Rejected));
}

TEST(ImportPreviewDialogTest, ValidationReturnsSameResult) {
    ImportPreviewDialog dlg(MakeResult(3, 2, 1));
    const auto& v = dlg.validation();
    EXPECT_EQ(v.total_rows, 6);
    EXPECT_EQ(v.InsertCount(), 3);
    EXPECT_EQ(v.UpdateCount(), 2);
    EXPECT_EQ(v.invalid_rows.size(), 1);
}

}  // namespace
