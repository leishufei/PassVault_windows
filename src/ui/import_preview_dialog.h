#pragma once

#include <QDialog>

#include "csv/csv_models.h"

class QLabel;
class QTabWidget;
class QTreeWidget;

namespace passvault::ui {

// Reviews a CsvValidationResult in three tabs (insert / update / invalid),
// mirroring Android's ImportPreviewDialog. Accepting confirms the import.
class ImportPreviewDialog : public QDialog {
    Q_OBJECT

 public:
    ImportPreviewDialog(csv::CsvValidationResult result,
                        QWidget* parent = nullptr);

    const csv::CsvValidationResult& validation() const { return result_; }

 private:
    void BuildInsertTab(QTabWidget* tabs);
    void BuildUpdateTab(QTabWidget* tabs);
    void BuildInvalidTab(QTabWidget* tabs);

    csv::CsvValidationResult result_;
    QLabel* summary_ = nullptr;
};

}  // namespace passvault::ui
