#pragma once

#include <QFrame>
#include <QList>
#include <QString>

#include "model/category.h"
#include "model/password_entry.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPropertyAnimation;
class QPushButton;
class QTextEdit;
class QToolButton;
class QWidget;

namespace passvault::ui {

class EditorPanel : public QFrame {
    Q_OBJECT

 public:
    enum class Mode { kCreate, kEdit };

    struct DecryptedEntry {
        model::PasswordEntry entry;
        QString password;
    };

    explicit EditorPanel(QWidget* parent = nullptr);
    ~EditorPanel() override;

    void SetCategories(QList<model::Category> categories);
    void OpenForCreate();
    void OpenForEdit(const DecryptedEntry& entry);
    void Close();
    bool IsOpen() const { return is_open_; }

    DecryptedEntry Result() const;
    void ApplyGeneratedPassword(const QString& password);

 signals:
    void SaveRequested();
    void CancelRequested();
    void GenerateRequested();

 protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

 private slots:
    void OnSaveClicked();
    void OnCancelClicked();
    void OnPasswordTextChanged(const QString& value);
    void OnTogglePasswordPreview();

 private:
    void BuildUi();
    QWidget* BuildOverviewPage();
    void FillCategoryCombo();
    void FillFromEntry(const DecryptedEntry& entry);
    void ResetFields();
    void UpdateStrength(int level);
    void UpdatePositionFromParent();
    void AnimateIn();
    void AnimateOut();
    void RefreshMode();

    Mode mode_ = Mode::kCreate;
    QList<model::Category> categories_;
    DecryptedEntry entry_;
    bool is_open_ = false;

    QLabel* header_title_ = nullptr;
    QToolButton* header_close_ = nullptr;
    QListWidget* navigation_ = nullptr;

    QLineEdit* title_input_ = nullptr;
    QLineEdit* website_input_ = nullptr;
    QLineEdit* username_input_ = nullptr;
    QLineEdit* password_input_ = nullptr;
    QWidget* password_field_ = nullptr;
    QToolButton* preview_toggle_ = nullptr;
    QToolButton* generate_button_ = nullptr;
    QComboBox* category_combo_ = nullptr;
    QTextEdit* notes_input_ = nullptr;

    QFrame* strength_segments_[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel* strength_label_ = nullptr;

    QPushButton* cancel_button_ = nullptr;
    QPushButton* save_button_ = nullptr;

    QPropertyAnimation* anim_ = nullptr;
};

}  // namespace passvault::ui
