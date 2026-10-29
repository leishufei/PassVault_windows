#pragma once

#include <QFrame>
#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

#include "model/category.h"
#include "model/password_entry.h"

class QLabel;
class QPushButton;
class QStackedLayout;
class QToolButton;
class QWidget;

namespace passvault::ui {

class ElidedLabel;

class DetailPanel : public QFrame {
    Q_OBJECT

 public:
    explicit DetailPanel(QWidget* parent = nullptr);
    ~DetailPanel() override;

    void SetCategories(QList<model::Category> categories);
    void SetEntry(const model::PasswordEntry& entry,
                  const std::optional<QString>& decrypted_password);
    void ClearEntry();

    bool HasEntry() const;
    std::int64_t entry_id() const;

 signals:
    void CloseRequested();
    void EditRequested(std::int64_t entry_id);
    void RestoreRequested(std::int64_t entry_id);
    void DeleteRequested(std::int64_t entry_id);
    void CopyPasswordRequested(std::int64_t entry_id);
    void CopyUsernameRequested(std::int64_t entry_id);
    void OpenWebsiteRequested(std::int64_t entry_id);
    void FavoriteToggleRequested(std::int64_t entry_id, bool desired);

 private:
    void BuildUi();
    QWidget* BuildEmptyPage();
    QWidget* BuildContentPage();
    void RefreshView();
    void RefreshThemeAssets();
    void UpdatePasswordDisplay();

    QString CategoryName(std::int64_t category_id) const;
    static QString FormatTimestamp(std::int64_t ms_since_epoch);
    static QString AvatarInitials(const model::PasswordEntry& entry);

    QStackedLayout* stack_ = nullptr;
    QWidget* empty_page_ = nullptr;
    QWidget* content_page_ = nullptr;
    QLabel* empty_icon_ = nullptr;

    QLabel* header_icon_ = nullptr;
    ElidedLabel* header_title_ = nullptr;
    QLabel* header_tag_ = nullptr;
    QToolButton* header_favorite_ = nullptr;
    QPushButton* header_edit_ = nullptr;
    QToolButton* header_delete_ = nullptr;
    QToolButton* header_close_ = nullptr;

    QWidget* risky_banner_ = nullptr;
    QLabel* risky_icon_ = nullptr;

    QLabel* username_value_ = nullptr;
    QToolButton* username_copy_ = nullptr;
    QLabel* password_value_ = nullptr;
    QToolButton* password_toggle_ = nullptr;
    QToolButton* password_copy_ = nullptr;
    QLabel* website_value_ = nullptr;
    QToolButton* website_open_ = nullptr;

    QLabel* created_value_ = nullptr;
    QLabel* updated_value_ = nullptr;

    QLabel* notes_value_ = nullptr;
    QLabel* title_value_ = nullptr;
    QLabel* category_value_ = nullptr;
    QLabel* package_value_ = nullptr;

    QList<model::Category> categories_;
    std::optional<model::PasswordEntry> entry_;
    std::optional<QString> password_plain_;
    bool password_visible_ = false;
};

}  // namespace passvault::ui
