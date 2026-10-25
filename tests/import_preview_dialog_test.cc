#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QHeaderView>
#include <QSignalSpy>
#include <QStringList>
#include <QTabWidget>
#include <QTest>
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

TEST(ImportPreviewDialogTest, UnchangedRowsCannotConfirmAnImport) {
    auto result = MakeResult(0, 1, 0);
    result.valid_rows[0].diffs.clear();
    ImportPreviewDialog dialog(result);
    EXPECT_FALSE(RoleButton(dialog, QDialogButtonBox::AcceptRole)->isEnabled());
    EXPECT_TRUE(Summary(dialog)->text().contains(QStringLiteral("无变化 1")));
    EXPECT_EQ(dialog.findChild<QTreeWidget*>(QStringLiteral("ImportUpdateTree"))
                  ->topLevelItemCount(), 0);
    EXPECT_TRUE(dialog.findChild<QLabel*>(QStringLiteral("ImportPreviewNotice"))
                    ->text().contains(QStringLiteral("无需导入")));
    EXPECT_EQ(dialog.validation().valid_rows.size(), 1);
}

TEST(ImportPreviewDialogTest, AllInvalidRowsOpenTheirTabAndExplainTheBlock) {
    ImportPreviewDialog dialog(MakeResult(0, 0, 2));
    EXPECT_EQ(Tabs(dialog)->currentIndex(), 2);
    EXPECT_FALSE(RoleButton(dialog, QDialogButtonBox::AcceptRole)->isEnabled());
    EXPECT_TRUE(dialog.findChild<QLabel*>(QStringLiteral("ImportPreviewNotice"))
                    ->text().contains(QStringLiteral("请先修正无效行")));
}

TEST(ImportPreviewDialogTest, PartialImportExplainsSkippedRows) {
    ImportPreviewDialog dialog(MakeResult(1, 1, 2));
    EXPECT_TRUE(RoleButton(dialog, QDialogButtonBox::AcceptRole)->isEnabled());
    EXPECT_TRUE(dialog.findChild<QLabel*>(QStringLiteral("ImportPreviewNotice"))
                    ->text().contains(QStringLiteral("可用的 2 行")));
}

TEST(ImportPreviewDialogTest, EmptyFileShowsEmptyStatesWithoutFakeRows) {
    ImportPreviewDialog dialog(MakeResult(0, 0, 0));
    EXPECT_FALSE(RoleButton(dialog, QDialogButtonBox::AcceptRole)->isEnabled());
    EXPECT_EQ(dialog.findChildren<QLabel*>(QStringLiteral("ImportPreviewEmpty"))
                  .size(), 3);
    for (auto* tree : dialog.findChildren<QTreeWidget*>()) {
        EXPECT_EQ(tree->topLevelItemCount(), 0);
    }
}

TEST(ImportPreviewDialogTest, PasswordDiffAndInvalidRawDataStayHidden) {
    auto result = MakeResult(0, 1, 1);
    result.valid_rows[0].diffs[0].field = QStringLiteral("密码");
    result.valid_rows[0].diffs[0].old_value = QStringLiteral("old-private-value");
    result.valid_rows[0].diffs[0].new_value = QStringLiteral("new-private-value");
    result.invalid_rows[0].raw_data.append(QStringLiteral("raw-private-value"));
    ImportPreviewDialog dialog(result);
    auto* updates = dialog.findChild<QTreeWidget*>(QStringLiteral("ImportUpdateTree"));
    auto* change = updates->topLevelItem(0)->child(0);
    EXPECT_TRUE(change->text(3).contains(QStringLiteral("已隐藏")));
    EXPECT_FALSE(change->text(3).contains(QStringLiteral("private-value")));
    EXPECT_TRUE(change->toolTip(3).isEmpty());
    auto* invalid = dialog.findChild<QTreeWidget*>(QStringLiteral("ImportInvalidTree"));
    EXPECT_FALSE(invalid->topLevelItem(0)->text(2).contains(QStringLiteral("private-value")));
    EXPECT_TRUE(invalid->topLevelItem(0)->toolTip(2).isEmpty());
    EXPECT_EQ(dialog.validation().valid_rows[0].diffs[0].new_value,
              result.valid_rows[0].diffs[0].new_value);
}

TEST(ImportPreviewDialogTest, LongTitlesStayInViewportAndKeepTheirTooltip) {
    auto result = MakeResult(1, 0, 0);
    result.valid_rows[0].title = QString(300, QLatin1Char('w'));
    ImportPreviewDialog dialog(result);
    dialog.show();
    QApplication::processEvents();
    auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("ImportInsertTree"));
    EXPECT_LE(tree->header()->length(), tree->viewport()->width());
    EXPECT_TRUE(tree->topLevelItem(0)->toolTip(1).contains(result.valid_rows[0].title));
}

TEST(ImportPreviewDialogTest, TooltipTreatsImportedMarkupAsLiteralText) {
    auto result = MakeResult(1, 0, 0);
    result.valid_rows[0].title = QStringLiteral("<b>literal title</b>");
    ImportPreviewDialog dialog(result);
    auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("ImportInsertTree"));
    const QString tooltip = tree->topLevelItem(0)->toolTip(1);
    EXPECT_FALSE(tooltip.contains(QStringLiteral("<b>")));
    EXPECT_TRUE(tooltip.contains(QStringLiteral("&lt;b&gt;")));
}

TEST(ImportPreviewDialogTest, EscapeCancelsWithoutAccepting) {
    ImportPreviewDialog dialog(MakeResult(1, 0, 0));
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    QSignalSpy rejected(&dialog, &QDialog::rejected);
    QTest::keyClick(&dialog, Qt::Key_Escape);
    EXPECT_EQ(accepted.count(), 0);
    EXPECT_EQ(rejected.count(), 1);
}

}  // namespace
