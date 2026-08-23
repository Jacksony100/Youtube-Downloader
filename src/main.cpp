#include "main_window.hpp"

#include <QApplication>

static int runApplication(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(vdp::kAppName);
    QCoreApplication::setOrganizationName("Jacksony");
    QCoreApplication::setApplicationVersion(VDP_VERSION);
    if (qEnvironmentVariableIsSet("VDP_SMOKE_TEST")) {
        vdp::ToolchainManager toolchain;
        return toolchain.ensureRuntime().ready() ? 0 : 2;
    }
    vdp::MainWindow window;
    window.show();
    return app.exec();
}

#ifdef Q_OS_WIN
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    char applicationName[] = "VideoDownloaderPro";
    char* arguments[] = {applicationName, nullptr};
    int argumentCount = 1;
    return runApplication(argumentCount, arguments);
}
#else
int main(int argc, char* argv[]) {
    return runApplication(argc, argv);
}
#endif
