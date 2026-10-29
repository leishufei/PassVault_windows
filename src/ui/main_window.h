#pragma once

#include <QMainWindow>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

#include "model/category.h"
#include "model/password_entry.h"
#include "sync/sync_manager.h"

class QAction;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QResizeEvent;
class QStackedWidget;
class QToolButton;
class QWidget;

namespace passvault::crypto {
class SessionKey;
}
namespace passvault::storage {
class PasswordDao;
class CategoryDao;
}
namespace passvault::sync {
class SyncScheduler;
}

namespace passvault::ui {

class DetailPanel;
class EditorPanel;
class PreferencesPage;

class MainWindow : public QMainWindow {
    Q_OBJECT

 public:
    struct Deps {
        storage::PasswordDao* password_dao = nullptr;
        storage::CategoryDao* category_dao = nullptr;
        const crypto::SessionKey* session_key = nullptr;
        sync::SyncManager* sync_manager = nullptr;
        sync::SyncScheduler* sync_scheduler = nullptr;
    };

    explicit MainWindow(const Deps& deps, QWidget* parent = nullptr);
    ~MainWindow() override;

    void Reload();
    void SetSessionKey(const crypto::SessionKey* session_key);
    PreferencesPage* preferences_page() const { return preferences_page_; }

 signals:
    void LockRequested();
    void OpenSettingsRequested();
    void ImportCsvRequested();
    void ExportCsvRequested();
    void MasterPasswordChangeRequested();

 protected:
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

 private slots:
    void OnNewPassword();
    void OnAddCategory();
    void OnRenameCategory(std::int64_t category_id);
    void OnDeleteCategory(std::int64_t category_id);
    void PersistCategoryOrder();
    void OnSectionSelectionChanged();
    void OnCategorySelectionChanged();
    void OnPasswordSelectionChanged();
    void OnSearchChanged(const QString& text);
    void OnSyncStarted();
    void OnSyncFinished(sync::SyncManager::StatusKind kind,
                        const QString& message);
    void OnEditRequested(std::int64_t entry_id);
    void OnDeleteRequested(std::int64_t entry_id);
    void OnDeleteSelected();
    void OnRestoreSelected();
    void OnEmptyTrash();
    void OnCopyPasswordRequested(std::int64_t entry_id);
    void OnCopyUsernameRequested(std::int64_t entry_id);
    void OnOpenWebsiteRequested(std::int64_t entry_id);
    void OnFavoriteToggleRequested(std::int64_t entry_id, bool desired);
    void OnEditorSaveRequested();
    void OnEditorCancelRequested();

 private:
    QWidget* BuildSidebar();
    QWidget* BuildWorkspaceHeader();
    QWidget* BuildPasswordListColumn();
    QWidget* BuildDetailColumn();
    void RefreshSectionList();
    void RefreshCategoryList();
    void RefreshPasswordList();
    void RefreshThemeAssets();
    void UpdateResponsiveLayout();
    void ShowListPane();
    void DeleteEntries(const std::vector<std::int64_t>& ids);
    void RestoreEntries(const std::vector<std::int64_t>& ids);
    void PermanentlyDeleteEntries(const std::vector<std::int64_t>& ids,
                                  bool empty_trash);
    void FinishTrashOperation(const std::optional<int>& count);
    void UpdateTrashActions();
    std::vector<std::int64_t> SelectedPasswordIds() const;
    void ToggleCompactSidebar();
    void UpdateSyncStatus(sync::SyncManager::StatusKind kind,
                          const QString& message);
    QWidget* CreatePasswordCard(const model::PasswordEntry& entry);
    std::optional<model::PasswordEntry> FindEntry(std::int64_t id) const;

    std::optional<QString> DecryptPassword(
        const model::PasswordEntry& entry) const;
    bool EncryptAndAssign(model::PasswordEntry* entry,
                          const QString& plaintext) const;

    void OpenEditDialog(std::int64_t entry_id);
    void ShowPasswordCreate();

    Deps deps_;
    QStackedWidget* central_stack_ = nullptr;
    QWidget* workspace_page_ = nullptr;
    QWidget* sidebar_ = nullptr;
    QWidget* workspace_ = nullptr;
    QWidget* workspace_content_ = nullptr;
    QHBoxLayout* workspace_content_layout_ = nullptr;
    PreferencesPage* preferences_page_ = nullptr;

    QListWidget* sections_list_ = nullptr;
    QListWidget* categories_list_ = nullptr;
    QToolButton* add_category_button_ = nullptr;
    QToolButton* navigation_button_ = nullptr;
    QToolButton* back_to_list_button_ = nullptr;
    QToolButton* more_button_ = nullptr;
    QWidget* search_container_ = nullptr;
    QLineEdit* search_ = nullptr;
    QAction* search_action_ = nullptr;
    QLabel* list_title_ = nullptr;
    QLabel* list_count_ = nullptr;
    QPushButton* restore_selected_button_ = nullptr;
    QPushButton* permanently_delete_button_ = nullptr;
    QPushButton* empty_trash_button_ = nullptr;
    QPushButton* sort_button_ = nullptr;
    QWidget* password_list_column_ = nullptr;
    QStackedWidget* list_content_stack_ = nullptr;
    QListWidget* password_list_ = nullptr;
    QLabel* empty_state_ = nullptr;
    DetailPanel* detail_panel_ = nullptr;
    EditorPanel* editor_panel_ = nullptr;
    std::int64_t editor_entry_id_ = -1;

    QLabel* sync_status_label_ = nullptr;
    QLabel* sync_status_dot_ = nullptr;

    std::int64_t selected_category_ = -1;
    QString search_text_;
    std::vector<model::PasswordEntry> current_entries_;
    std::vector<model::Category> categories_;
    bool suppress_selection_ = false;
    bool showing_detail_ = false;
    bool compact_sidebar_expanded_ = false;
    enum class SortOrder { kUpdated, kCreated, kTitle };
    SortOrder sort_order_ = SortOrder::kUpdated;
    bool sort_ascending_ = false;
};

}  // namespace passvault::ui
