#pragma once


#include "csv/csv_models.h"
#include "ui/app_dialog.h"

class QLabel;
class QTabWidget;
class QTreeWidget;

namespace passvault::ui {

// Reviews a CsvValidationResult in three tabs (insert / update / invalid),
// mirroring Android's ImportPreviewDialog. Accepting confirms the import.
class ImportPreviewDialog : public AppDialog {
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
