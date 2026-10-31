#include "ui/theme_manager.h"

#include <QApplication>
#include <QAbstractButton>
#include <QComboBox>
#include <QEvent>
#include <QFile>
#include <QFrame>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QHash>
#include <QMenu>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QCheckBox>
#include <QPainterPath>
#include <QRegion>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleHints>
#include <QStyle>
#include <QTextStream>

#include "ui/icon_loader.h"

namespace passvault::ui {

namespace {

constexpr const char* kSettingsKey = "ui/theme";

Theme ThemeFromString(const QString& value) {
    if (value == QStringLiteral("light")) return Theme::kLight;
    if (value == QStringLiteral("dark")) return Theme::kDark;
    return Theme::kSystem;
}

QString StringFromTheme(Theme theme) {
    switch (theme) {
        case Theme::kLight:
            return QStringLiteral("light");
        case Theme::kDark:
            return QStringLiteral("dark");
        case Theme::kSystem:
        default:
            return QStringLiteral("system");
    }
}

QHash<QString, QString> ParseTokens(const QString& css) {
    QHash<QString, QString> tokens;
    static const QRegularExpression re(
        QStringLiteral("--([A-Za-z0-9_-]+):\\s*([^;\\n]+);"));
    auto it = re.globalMatch(css);
    while (it.hasNext()) {
        const auto m = it.next();
        tokens.insert(m.captured(1).trimmed(), m.captured(2).trimmed());
    }
    return tokens;
}

QString SubstituteTokens(QString css,
                         const QHash<QString, QString>& tokens) {
    static const QRegularExpression re(
        QStringLiteral("var\\(\\s*--([A-Za-z0-9_-]+)\\s*\\)"));
    QString out;
    out.reserve(css.size());
    int pos = 0;
    auto it = re.globalMatch(css);
    while (it.hasNext()) {
        const auto m = it.next();
        out.append(css.mid(pos, m.capturedStart() - pos));
        const QString key = m.captured(1);
        out.append(tokens.value(key, m.captured(0)));
        pos = m.capturedEnd();
    }
    out.append(css.mid(pos));
    return out;
}

QColor ParseColorValue(const QString& value) {
    const QColor color(value);
    if (color.isValid()) return color;

    static const QRegularExpression rgba(
        QStringLiteral(
            R"(^rgba\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)$)"));
    const auto match = rgba.match(value);
    if (!match.hasMatch()) return {};
    return QColor(match.captured(1).toInt(), match.captured(2).toInt(),
                  match.captured(3).toInt(), match.captured(4).toInt());
}

}  // namespace

ThemeManager* ThemeManager::Instance() {
    static ThemeManager* instance = new ThemeManager();
    return instance;
}

ThemeManager::ThemeManager() {
    if (qApp) qApp->installEventFilter(this);
    QSettings settings;
    theme_ = ThemeFromString(
        settings.value(kSettingsKey, QStringLiteral("system")).toString());
    RefreshPalette();
    if (auto* hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this](auto) {
            if (theme_ == Theme::kSystem && !applying_theme_) {
                ApplyTheme(Theme::kSystem);
            }
        });
    }
}

ThemeManager::~ThemeManager() = default;

bool ThemeManager::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget) return QObject::eventFilter(watched, event);
    const bool dialog = qobject_cast<QDialog*>(widget) &&
        !qobject_cast<QFileDialog*>(widget) &&
        widget->objectName() != QStringLiteral("MasterPasswordDialog");
    const bool popup = qobject_cast<QMenu*>(widget) || widget->windowType() == Qt::ToolTip;
    if (event->type() == QEvent::Polish) {
        if (qobject_cast<QCheckBox*>(widget)) widget->setFocusPolicy(Qt::TabFocus);
        if (dialog || popup) {
            widget->setWindowFlag(Qt::FramelessWindowHint);
            widget->setWindowFlag(Qt::NoDropShadowWindowHint);
            widget->setAttribute(Qt::WA_TranslucentBackground);
        }
        if (dialog) {
            widget->setContentsMargins(16, 12, 16, 12);
            if (!widget->findChild<QFrame*>(QStringLiteral("ConfirmationSurface"))) {
                auto* surface = new QFrame(widget);
                surface->setObjectName(QStringLiteral("ConfirmationSurface"));
                surface->setAttribute(Qt::WA_TransparentForMouseEvents);
                surface->show();
                surface->lower();
            }
            if (auto* message = qobject_cast<QMessageBox*>(widget)) {
                message->setOption(QMessageBox::Option::DontUseNativeDialog);
                message->setIcon(QMessageBox::NoIcon);
                message->setMinimumWidth(360);
                message->setMinimumHeight(150);
                if (auto* button = message->button(QMessageBox::Yes)) {
                    button->setText(QStringLiteral("确认"));
                    button->setProperty("accent", true);
                }
                if (auto* button = message->button(QMessageBox::No)) {
                    button->setText(QStringLiteral("取消"));
                }
            }
            for (auto* buttons : widget->findChildren<QDialogButtonBox*>()) {
                if (auto* button = buttons->button(QDialogButtonBox::Ok)) {
                    button->setText(QStringLiteral("确认"));
                    button->setProperty("accent", true);
                }
                if (auto* button = buttons->button(QDialogButtonBox::Cancel)) {
                    button->setText(QStringLiteral("取消"));
                }
            }
        }
    }
    if ((dialog || popup) && (event->type() == QEvent::Show ||
                              event->type() == QEvent::Resize)) {
        const int radius = dialog ? 14 : 6;
        QPainterPath outline;
        outline.addRoundedRect(QRectF(widget->rect()), radius, radius);
        widget->setMask(QRegion(outline.toFillPolygon().toPolygon()));
        if (dialog) {
            if (auto* surface = widget->findChild<QFrame*>(QStringLiteral("ConfirmationSurface"))) {
                surface->setGeometry(widget->rect());
                surface->setStyleSheet(QStringLiteral(
                    "QFrame#ConfirmationSurface { background-color: %1; border: 1px solid %2; border-radius: 14px; }")
                    .arg(ColorHex(QStringLiteral("bg-canvas")), ColorHex(QStringLiteral("border-panel"))));
                surface->lower();
            }
        }
    }
    if (event->type() == QEvent::MouseButtonPress && qobject_cast<QCheckBox*>(widget)) {
        widget->clearFocus();
    }
    if (qobject_cast<QAbstractButton*>(widget) || qobject_cast<QComboBox*>(widget)) {
        bool keyboard = false;
        if (event->type() == QEvent::FocusIn) {
            const auto reason = static_cast<QFocusEvent*>(event)->reason();
            keyboard = reason == Qt::TabFocusReason ||
                       reason == Qt::BacktabFocusReason ||
                       reason == Qt::ShortcutFocusReason;
        } else if (event->type() != QEvent::MouseButtonPress &&
                   event->type() != QEvent::FocusOut) {
            return QObject::eventFilter(watched, event);
        }
        if (widget->property("keyboardFocus").toBool() != keyboard) {
            widget->setProperty("keyboardFocus", keyboard);
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
            widget->update();
        }
    }
    return QObject::eventFilter(watched, event);
}

void ThemeManager::ApplyTheme(Theme theme) {
    if (applying_theme_) return;
    applying_theme_ = true;
    theme_ = theme;
    QSettings().setValue(kSettingsKey, StringFromTheme(theme));
    RefreshPalette();
    IconLoader::ClearCache();
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        app->setStyleSheet(LoadStyleSheet());
    }
    emit ThemeChanged(effective_theme_);
    applying_theme_ = false;
}

Theme ThemeManager::DetectSystemTheme() const {
    if (auto* hints = QGuiApplication::styleHints()) {
        if (hints->colorScheme() == Qt::ColorScheme::Light) return Theme::kLight;
    }
    return Theme::kDark;
}

void ThemeManager::RefreshPalette() {
    effective_theme_ = (theme_ == Theme::kSystem) ? DetectSystemTheme() : theme_;
}

QString ThemeManager::ReadResource(const QString& path) const {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

QString ThemeManager::LoadStyleSheet() const {
    const QString tokens_path =
        effective_theme_ == Theme::kLight
            ? QStringLiteral(":/qss/tokens_light.qss")
            : QStringLiteral(":/qss/tokens_dark.qss");
    const QString tokens_css = ReadResource(tokens_path);
    const QString components_css = ReadResource(QStringLiteral(":/qss/components.qss"));
    const QString app_css = ReadResource(QStringLiteral(":/qss/app.qss"));
    const auto tokens = ParseTokens(tokens_css);
    return SubstituteTokens(components_css + QLatin1Char('\n') + app_css,
                            tokens);
}

QColor ThemeManager::Color(const QString& token) const {
    const QString tokens_path =
        effective_theme_ == Theme::kLight
            ? QStringLiteral(":/qss/tokens_light.qss")
            : QStringLiteral(":/qss/tokens_dark.qss");
    const auto tokens = ParseTokens(ReadResource(tokens_path));
    return ParseColorValue(tokens.value(token));
}

QString ThemeManager::ColorHex(const QString& token) const {
    const QString tokens_path =
        effective_theme_ == Theme::kLight
            ? QStringLiteral(":/qss/tokens_light.qss")
            : QStringLiteral(":/qss/tokens_dark.qss");
    return ParseTokens(ReadResource(tokens_path)).value(token);
}

}  // namespace passvault::ui
