#include "ui/theme_manager.h"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleHints>
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
