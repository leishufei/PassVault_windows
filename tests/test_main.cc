#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "ui/theme_manager.h"

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    passvault::ui::ThemeManager::ConfigurePlatform();
    QApplication app(argc, argv);
    QTemporaryDir settings_dir;
    if (!settings_dir.isValid()) return 1;
    app.setOrganizationName("PassVaultTests");
    app.setApplicationName("PassVaultTests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                        settings_dir.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                        settings_dir.path());

    passvault::ui::ThemeManager::InitializeTypography();
    app.setStyleSheet(
        passvault::ui::ThemeManager::Instance()->LoadStyleSheet());

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
