#include <QApplication>
#include <QMainWindow>

#include "app/application.h"

int main(int argc, char* argv[]) {
    QApplication qt_app(argc, argv);
    qt_app.setApplicationName("PassVault");
    qt_app.setOrganizationName("PassVault");

    passvault::app::Application application;
    return application.Run();
}
