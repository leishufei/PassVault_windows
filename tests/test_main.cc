#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "ui/theme_manager.h"

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
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

#ifdef Q_OS_WIN
    if (app.platformName() == QStringLiteral("offscreen")) {
        const QDir fonts(qEnvironmentVariable("WINDIR") + "/Fonts");
        for (const char* name : {"SegUIVar.ttf", "segoeui.ttf", "seguisb.ttf",
                                 "segoeuib.ttf", "msyh.ttc", "consola.ttf",
                                 "consolab.ttf"}) {
            const QString path = fonts.filePath(QString::fromLatin1(name));
            if (QFile::exists(path) &&
                QFontDatabase::addApplicationFont(path) < 0) {
                return 1;
            }
        }
    }
#endif
    QFont app_font;
    app_font.setFamilies({QStringLiteral("Segoe UI Variable Text"),
                          QStringLiteral("Segoe UI"),
                          QStringLiteral("Microsoft YaHei UI"),
                          QStringLiteral("Microsoft YaHei"),
                          QStringLiteral("Roboto"),
                          QStringLiteral("Helvetica Neue"),
                          QStringLiteral("Arial")});
    app_font.setPixelSize(13);
    app.setFont(app_font);
    app.setStyleSheet(
        passvault::ui::ThemeManager::Instance()->LoadStyleSheet());

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
