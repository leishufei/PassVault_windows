#pragma once

#include <QString>

#include <array>

#include "generator/password_generator.h"
#include "ui/app_dialog.h"

class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QToolButton;

namespace passvault::ui {

// Standalone random password generator, reachable from the sidebar and
// Ctrl+G without opening the entry editor. Uses the same controls as the
// editor's generator page (8-32 length slider, four character groups,
// strength meter) inside the shared AppDialog surface. Copying uses the
// sensitive clipboard, which clears itself after the configured timeout.
class GeneratorDialog : public AppDialog {
    Q_OBJECT

 public:
    static constexpr int kMinLength = 8;
    static constexpr int kMaxLength = 32;
    static constexpr int kDefaultLength = 16;

    explicit GeneratorDialog(QWidget* parent = nullptr);
    ~GeneratorDialog() override;

    // The password currently shown (empty when no character group is set).
    QString password() const;

 private slots:
    void Regenerate();
    void CopyPassword();

 private:
    void BuildUi();
    generator::PasswordConfig CurrentConfig() const;
    void UpdateStrengthUi(const QString& password);
    void RefreshIcons();

    QLineEdit* preview_ = nullptr;
    QToolButton* refresh_ = nullptr;
    QToolButton* copy_ = nullptr;
    QLabel* length_value_ = nullptr;
    QSlider* length_slider_ = nullptr;
    QCheckBox* upper_ = nullptr;
    QCheckBox* lower_ = nullptr;
    QCheckBox* number_ = nullptr;
    QCheckBox* symbol_ = nullptr;
    std::array<QFrame*, 4> strength_segments_{};
    QLabel* strength_label_ = nullptr;
    QLabel* error_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* copy_button_ = nullptr;
};

}  // namespace passvault::ui
