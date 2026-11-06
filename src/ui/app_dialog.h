#pragma once

#include <QDialog>
#include <QHash>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QString>

#include <vector>

class QAbstractButton;
class QFrame;
class QGraphicsDropShadowEffect;
class QHBoxLayout;
class QHideEvent;
class QLabel;
class QPushButton;
class QShowEvent;
class QToolButton;
class QVBoxLayout;

namespace passvault::ui {

// The single modal surface used by every PassVault popup. It follows the
// master password dialog: the parent window is dimmed with a backdrop, the
// content sits on a 14px card with a hairline border and a soft shadow, and
// the card is split into a header (icon tile, title, close button), a body
// and a footer with right-aligned actions (secondary first, primary last).
//
// The button API intentionally mirrors QMessageBox (standard buttons,
// defaultButton(), clickedButton(), text()) so call sites and tests can move
// over without changing their control flow.
class AppDialog : public QDialog {
    Q_OBJECT

 public:
    using StandardButton = QMessageBox::StandardButton;
    using StandardButtons = QMessageBox::StandardButtons;

    enum class Tone { kInfo, kQuestion, kWarning, kDanger };
    enum class ButtonStyle { kAuto, kPrimary, kSecondary, kDanger };

    explicit AppDialog(const QString& title, QWidget* parent = nullptr);
    ~AppDialog() override;

    void SetTitle(const QString& title);
    QString title() const;
    // Picks the header icon and its color from the tone.
    void SetTone(Tone tone);
    void SetIcon(const QString& icon_name, const QString& color_token);
    // Plain-text explanation shown at the top of the body.
    void SetMessage(const QString& text);
    QString text() const;
    // Width of the visible card; the window adds room for the shadow.
    void SetCardWidth(int width);
    QVBoxLayout* body_layout() const { return body_layout_; }
    QWidget* body() const { return body_; }

    QPushButton* AddButton(StandardButton which,
                           ButtonStyle style = ButtonStyle::kAuto);
    QPushButton* AddButton(const QString& text, QMessageBox::ButtonRole role,
                           ButtonStyle style = ButtonStyle::kAuto);
    QPushButton* button(StandardButton which) const;
    void SetButtonStyle(QPushButton* button, ButtonStyle style);
    void SetDefaultButton(QPushButton* button);
    void SetDefaultButton(StandardButton which);
    QPushButton* defaultButton() const { return default_button_; }
    void SetEscapeButton(QPushButton* button);
    void SetEscapeButton(StandardButton which);
    QAbstractButton* clickedButton() const { return clicked_button_; }
    StandardButton standardButton(QAbstractButton* button) const;
    // The standard button that closed the dialog, including Esc / close and
    // programmatic accept() / reject().
    StandardButton ResultButton() const;

    // Adds a labelled single-line input to the body (QInputDialog style).
    QLineEdit* AddTextField(const QString& label, const QString& value = {},
                            QLineEdit::EchoMode mode = QLineEdit::Normal,
                            const QString& placeholder = {});
    QLineEdit* text_field() const { return text_field_; }
    void setTextValue(const QString& value);
    QString textValue() const;

    static StandardButton Information(
        QWidget* parent, const QString& title, const QString& text,
        StandardButtons buttons = QMessageBox::Ok,
        StandardButton default_button = QMessageBox::NoButton);
    static StandardButton Warning(
        QWidget* parent, const QString& title, const QString& text,
        StandardButtons buttons = QMessageBox::Ok,
        StandardButton default_button = QMessageBox::NoButton);
    static StandardButton Critical(
        QWidget* parent, const QString& title, const QString& text,
        StandardButtons buttons = QMessageBox::Ok,
        StandardButton default_button = QMessageBox::NoButton);
    static StandardButton Question(
        QWidget* parent, const QString& title, const QString& text,
        StandardButtons buttons = QMessageBox::Yes | QMessageBox::No,
        StandardButton default_button = QMessageBox::NoButton);
    // kNormal: primary (blue) confirm button; kDestructive: red confirm
    // button for deleting / clearing data. In both, Enter confirms and Esc
    // cancels.
    enum class ConfirmKind { kNormal, kDestructive };
    // Yes / No confirmation. Returns true when the confirm button was chosen.
    static bool Confirm(QWidget* parent, const QString& title,
                        const QString& text, const QString& confirm_text,
                        ConfirmKind kind = ConfirmKind::kNormal);
    static QString GetText(QWidget* parent, const QString& title,
                           const QString& label,
                           QLineEdit::EchoMode mode = QLineEdit::Normal,
                           const QString& value = {}, bool* ok = nullptr,
                           const QString& placeholder = {});

 public slots:
    void reject() override;

 protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

 private:
    struct ButtonInfo {
        QMessageBox::ButtonRole role = QMessageBox::InvalidRole;
        StandardButton standard = QMessageBox::NoButton;
    };

    static StandardButton ShowMessage(QWidget* parent, Tone tone,
                                      const QString& title,
                                      const QString& text,
                                      StandardButtons buttons,
                                      StandardButton default_button);
    void OnButtonClicked(QPushButton* button);
    void RefreshThemeAssets();

    QFrame* card_ = nullptr;
    QGraphicsDropShadowEffect* shadow_ = nullptr;
    QLabel* icon_ = nullptr;
    QLabel* title_ = nullptr;
    QToolButton* close_button_ = nullptr;
    QWidget* body_ = nullptr;
    QVBoxLayout* body_layout_ = nullptr;
    QLabel* message_ = nullptr;
    QWidget* footer_ = nullptr;
    QHBoxLayout* footer_layout_ = nullptr;
    QLineEdit* text_field_ = nullptr;
    QString icon_name_;
    QString icon_token_;

    std::vector<QPushButton*> buttons_;
    QHash<QPushButton*, ButtonInfo> button_info_;
    QPushButton* default_button_ = nullptr;
    QPushButton* escape_button_ = nullptr;
    QAbstractButton* clicked_button_ = nullptr;
    QPointer<QWidget> backdrop_;
};

}  // namespace passvault::ui
