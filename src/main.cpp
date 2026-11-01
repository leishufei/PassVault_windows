#include <QApplication>
#include <QIcon>
#include <QMainWindow>

#include "app/application.h"
#include "ui/theme_manager.h"

int main(int argc, char* argv[]) {
    passvault::ui::ThemeManager::ConfigurePlatform();
    QApplication qt_app(argc, argv);
    qt_app.setApplicationName("PassVault");
    qt_app.setOrganizationName("PassVault");
    qt_app.setWindowIcon(QIcon(QStringLiteral(":/icons/passvault.png")));

    passvault::app::Application application;
    return application.Run();
}
