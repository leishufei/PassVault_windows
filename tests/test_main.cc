#include <QApplication>
#include <QFont>
#include <QFontDatabase>

#include <gtest/gtest.h>

#include "ui/theme_manager.h"

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    static constexpr const char* kFontResources[] = {
        ":/fonts/NotoSansSC-VariableFont_wght.ttf",
        ":/fonts/DMMono-Regular.ttf",
        ":/fonts/DMMono-Medium.ttf",
    };
    for (const char* path : kFontResources) {
        if (QFontDatabase::addApplicationFont(QString::fromLatin1(path)) < 0) {
            return 1;
        }
    }
    QFont app_font;
    app_font.setFamilies({QStringLiteral("Noto Sans SC"),
                          QStringLiteral("Microsoft YaHei UI"),
                          QStringLiteral("Segoe UI")});
    app_font.setPixelSize(13);
    app.setFont(app_font);
    app.setStyleSheet(
        passvault::ui::ThemeManager::Instance()->LoadStyleSheet());

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
