#include "ui/import_preview_dialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace passvault::ui {

namespace {

QString Preview(const QString& s) {
    QString collapsed = s;
    collapsed.replace(QLatin1Char('\n'), QLatin1Char(' '));
    collapsed.replace(QLatin1Char('\r'), QLatin1Char(' '));
    if (collapsed.size() > 60) collapsed = collapsed.left(57) + QStringLiteral("...");
    return collapsed;
}

}  // namespace

ImportPreviewDialog::ImportPreviewDialog(csv::CsvValidationResult result,
                                         QWidget* parent)
    : QDialog(parent), result_(std::move(result)) {
    setObjectName(QStringLiteral("ImportPreviewDialog"));
    setWindowTitle(QStringLiteral("导入预览"));
    setModal(true);
    resize(760, 520);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("导入预览"), this);
    title->setObjectName(QStringLiteral("DialogTitle"));
    root->addWidget(title);

    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("DialogSubtitle"));
    summary_->setWordWrap(true);
    summary_->setText(
        QStringLiteral("共 %1 行 · 新增 %2 · 更新 %3 · 无效 %4")
            .arg(result_.total_rows)
            .arg(result_.InsertCount())
            .arg(result_.UpdateCount())
            .arg(result_.invalid_rows.size()));
    root->addWidget(summary_);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("ImportPreviewTabs"));
    BuildInsertTab(tabs);
    BuildUpdateTab(tabs);
    BuildInvalidTab(tabs);
    root->addWidget(tabs, 1);

    auto* box = new QDialogButtonBox(this);
    auto* cancel = box->addButton(QStringLiteral("取消"),
                                  QDialogButtonBox::RejectRole);
    auto* confirm = box->addButton(QStringLiteral("确认导入"),
                                   QDialogButtonBox::AcceptRole);
    confirm->setProperty("accent", true);
    confirm->setEnabled(!result_.valid_rows.isEmpty());
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, this, &QDialog::accept);
    root->addWidget(box);
}

void ImportPreviewDialog::BuildInsertTab(QTabWidget* tabs) {
    auto* tree = new QTreeWidget;
    tree->setColumnCount(4);
    tree->setHeaderLabels({QStringLiteral("行"), QStringLiteral("标题"),
                           QStringLiteral("用户名"), QStringLiteral("分类")});
    tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tree->setRootIsDecorated(false);
    for (const auto& row : result_.valid_rows) {
        if (row.action != csv::ImportAction::kInsert) continue;
        auto* item = new QTreeWidgetItem(tree);
        item->setText(0, QString::number(row.row_index));
        item->setText(1, Preview(row.title));
        item->setText(2, Preview(row.username));
        item->setText(3, Preview(row.category_name));
    }
    tabs->addTab(tree, QStringLiteral("新增 (%1)").arg(result_.InsertCount()));
}

void ImportPreviewDialog::BuildUpdateTab(QTabWidget* tabs) {
    auto* tree = new QTreeWidget;
    tree->setColumnCount(4);
    tree->setHeaderLabels({QStringLiteral("行"), QStringLiteral("标题"),
                           QStringLiteral("字段"), QStringLiteral("变更")});
    tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    for (const auto& row : result_.valid_rows) {
        if (row.action != csv::ImportAction::kUpdate) continue;
        auto* parent = new QTreeWidgetItem(tree);
        parent->setText(0, QString::number(row.row_index));
        parent->setText(1, Preview(row.title));
        parent->setText(
            2, QStringLiteral("%1 个字段").arg(row.diffs.size()));
        for (const auto& d : row.diffs) {
            auto* child = new QTreeWidgetItem(parent);
            child->setText(2, d.field);
            child->setText(3, QStringLiteral("%1 → %2")
                                  .arg(Preview(d.old_value),
                                       Preview(d.new_value)));
        }
        parent->setExpanded(false);
    }
    tabs->addTab(tree, QStringLiteral("更新 (%1)").arg(result_.UpdateCount()));
}

void ImportPreviewDialog::BuildInvalidTab(QTabWidget* tabs) {
    auto* tree = new QTreeWidget;
    tree->setColumnCount(3);
    tree->setHeaderLabels({QStringLiteral("行"), QStringLiteral("原因"),
                           QStringLiteral("原始数据")});
    tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tree->setRootIsDecorated(false);
    for (const auto& r : result_.invalid_rows) {
        auto* item = new QTreeWidgetItem(tree);
        item->setText(0, QString::number(r.row_index));
        item->setText(1, r.error_message);
        item->setText(2, Preview(r.raw_data.join(QLatin1Char(','))));
    }
    tabs->addTab(tree,
                 QStringLiteral("无效 (%1)").arg(result_.invalid_rows.size()));
}

}  // namespace passvault::ui
