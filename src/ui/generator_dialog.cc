#include "ui/generator_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>

#include "generator/password_strength.h"
#include "ui/clipboard_manager.h"

namespace passvault::ui {

GeneratorDialog::GeneratorDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("GeneratorDialog"));
    setWindowTitle(QStringLiteral("生成密码"));
    setModal(true);
    setMinimumWidth(460);
    BuildUi();
    Regenerate();
}

QString GeneratorDialog::password() const { return password_; }

void GeneratorDialog::BuildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("生成密码"), this);
    title->setObjectName(QStringLiteral("DialogTitle"));
    root->addWidget(title);

    preview_ = new QLineEdit(this);
    preview_->setReadOnly(true);
    preview_->setObjectName(QStringLiteral("DetailMonoValue"));
    preview_->setProperty("mono", true);

    auto* refresh = new QPushButton(QStringLiteral("重新生成"), this);
    refresh->setProperty("flat", true);
    connect(refresh, &QPushButton::clicked, this, &GeneratorDialog::Regenerate);

    auto* copy = new QPushButton(QStringLiteral("复制"), this);
    copy->setProperty("flat", true);
    connect(copy, &QPushButton::clicked, this, [this]() {
        ClipboardManager::Instance()->CopySensitive(preview_->text());
    });

    auto* preview_row = new QHBoxLayout;
    preview_row->addWidget(preview_, 1);
    preview_row->addWidget(refresh);
    preview_row->addWidget(copy);
    root->addLayout(preview_row);

    strength_ = new QProgressBar(this);
    strength_->setObjectName(QStringLiteral("StrengthBar"));
    strength_->setRange(0, 3);
    strength_->setTextVisible(false);
    strength_->setFixedHeight(6);
    strength_label_ = new QLabel(this);
    strength_label_->setObjectName(QStringLiteral("DialogSubtitle"));
    auto* strength_row = new QHBoxLayout;
    strength_row->addWidget(strength_, 1);
    strength_row->addWidget(strength_label_);
    root->addLayout(strength_row);

    length_slider_ = new QSlider(Qt::Horizontal, this);
    length_slider_->setObjectName(QStringLiteral("GeneratorLengthSlider"));
    length_slider_->setRange(4, 64);
    length_slider_->setValue(16);
    length_spin_ = new QSpinBox(this);
    length_spin_->setObjectName(QStringLiteral("GeneratorLengthSpin"));
    length_spin_->setRange(4, 64);
    length_spin_->setValue(16);
    connect(length_slider_, &QSlider::valueChanged, length_spin_,
            &QSpinBox::setValue);
    connect(length_spin_,
            static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged),
            length_slider_, &QSlider::setValue);
    connect(length_slider_, &QSlider::valueChanged, this,
            [this](int) { Regenerate(); });

    auto* len_row = new QHBoxLayout;
    len_row->addWidget(new QLabel(QStringLiteral("长度"), this));
    len_row->addWidget(length_slider_, 1);
    len_row->addWidget(length_spin_);
    root->addLayout(len_row);

    upper_ = new QCheckBox(QStringLiteral("A - Z 大写字母"), this);
    upper_->setObjectName(QStringLiteral("GeneratorUpper"));
    lower_ = new QCheckBox(QStringLiteral("a - z 小写字母"), this);
    lower_->setObjectName(QStringLiteral("GeneratorLower"));
    number_ = new QCheckBox(QStringLiteral("0 - 9 数字"), this);
    number_->setObjectName(QStringLiteral("GeneratorNumber"));
    symbol_ = new QCheckBox(QStringLiteral("!@#$ 特殊符号"), this);
    symbol_->setObjectName(QStringLiteral("GeneratorSymbol"));
    for (QCheckBox* c : {upper_, lower_, number_, symbol_}) {
        c->setChecked(true);
        connect(c, &QCheckBox::toggled, this, [this](bool) { Regenerate(); });
    }
    auto* grid = new QGridLayout;
    grid->addWidget(upper_, 0, 0);
    grid->addWidget(lower_, 0, 1);
    grid->addWidget(number_, 1, 0);
    grid->addWidget(symbol_, 1, 1);
    root->addLayout(grid);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("FormError"));
    error_->setVisible(false);
    root->addWidget(error_);

    auto* box = new QDialogButtonBox(this);
    auto* cancel = box->addButton(QDialogButtonBox::Cancel);
    cancel->setText(QStringLiteral("取消"));
    auto* ok = box->addButton(QStringLiteral("使用此密码"),
                              QDialogButtonBox::AcceptRole);
    ok->setProperty("accent", true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &GeneratorDialog::OnAccept);
    root->addWidget(box);
}

generator::PasswordConfig GeneratorDialog::CurrentConfig() const {
    generator::PasswordConfig c;
    c.length = length_slider_ ? length_slider_->value() : 16;
    c.include_uppercase = upper_ && upper_->isChecked();
    c.include_lowercase = lower_ && lower_->isChecked();
    c.include_numbers = number_ && number_->isChecked();
    c.include_symbols = symbol_ && symbol_->isChecked();
    return c;
}

void GeneratorDialog::UpdateStrengthUi(const QString& password) {
    const int level = generator::CalculatePasswordStrength(password);
    strength_->setValue(level);
    strength_->setProperty("level", level);
    strength_->style()->unpolish(strength_);
    strength_->style()->polish(strength_);
    const QString names[] = {QStringLiteral("弱"), QStringLiteral("中"),
                             QStringLiteral("强")};
    strength_label_->setText(QStringLiteral("强度: %1")
                                 .arg(names[qBound(0, level - 1, 2)]));
}

void GeneratorDialog::Regenerate() {
    error_->setVisible(false);
    const auto config = CurrentConfig();
    const auto opt = generator::PasswordGenerator::Generate(config);
    if (!opt.has_value()) {
        preview_->clear();
        error_->setText(QStringLiteral("至少选择一类字符。"));
        error_->setVisible(true);
        strength_->setValue(0);
        strength_label_->clear();
        return;
    }
    preview_->setText(*opt);
    UpdateStrengthUi(*opt);
}

void GeneratorDialog::OnAccept() {
    if (preview_->text().isEmpty()) return;
    password_ = preview_->text();
    accept();
}

}  // namespace passvault::ui
