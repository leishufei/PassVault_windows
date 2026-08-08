#pragma once

#include <QColor>
#include <QObject>
#include <QString>

namespace passvault::ui {

enum class Theme {
    kSystem,
    kLight,
    kDark,
};

class ThemeManager : public QObject {
    Q_OBJECT

 public:
    static ThemeManager* Instance();

    Theme theme() const { return theme_; }
    Theme effective_theme() const { return effective_theme_; }

    void ApplyTheme(Theme theme);

    QColor Color(const QString& token) const;
    QString ColorHex(const QString& token) const;

    QString LoadStyleSheet() const;

 signals:
    void ThemeChanged(Theme effective_theme);

 private:
    ThemeManager();
    ~ThemeManager() override;
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    Theme DetectSystemTheme() const;
    void RefreshPalette();
    QString ReadResource(const QString& path) const;

    Theme theme_ = Theme::kSystem;
    Theme effective_theme_ = Theme::kDark;
    bool applying_theme_ = false;
};

}  // namespace passvault::ui
