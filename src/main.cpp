#include "main_window.hpp"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(vdp::kAppName);
    QCoreApplication::setOrganizationName("Jacksony");
    QCoreApplication::setApplicationVersion(VDP_VERSION);
    vdp::MainWindow window;
    window.show();
    return app.exec();
}
