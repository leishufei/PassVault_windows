#pragma once

#include <QDialog>
#include <QString>

#include "generator/password_generator.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QSlider;
class QSpinBox;

namespace passvault::ui {

class GeneratorDialog : public QDialog {
    Q_OBJECT

 public:
    explicit GeneratorDialog(QWidget* parent = nullptr);

    QString password() const;

 private slots:
    void Regenerate();
    void OnAccept();

 private:
    void BuildUi();
    generator::PasswordConfig CurrentConfig() const;
    void UpdateStrengthUi(const QString& password);

    QSlider* length_slider_ = nullptr;
    QSpinBox* length_spin_ = nullptr;
    QCheckBox* upper_ = nullptr;
    QCheckBox* lower_ = nullptr;
    QCheckBox* number_ = nullptr;
    QCheckBox* symbol_ = nullptr;
    QLineEdit* preview_ = nullptr;
    QProgressBar* strength_ = nullptr;
    QLabel* strength_label_ = nullptr;
    QLabel* error_ = nullptr;

    QString password_;
};

}  // namespace passvault::ui
