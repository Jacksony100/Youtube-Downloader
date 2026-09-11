#include "toolchain_service.hpp"
#include <QLockFile>
#include <QDir>
#include <QThread>
#include <memory>

namespace vdp {
ToolchainService::ToolchainService(AppPaths paths, QObject* parent) : QObject(parent), paths_(std::move(paths)) {
    qRegisterMetaType<ToolchainStatus>();
}
void ToolchainService::ensure() { run(true); }
void ToolchainService::refresh() { run(false); }
void ToolchainService::repair() { run(true); }
void ToolchainService::run(bool provision) {
    if (busy_) return;
    busy_ = true;
    struct Result { ToolchainStatus status; QString error; };
    const auto result = std::make_shared<Result>();
    auto* thread = QThread::create([paths = paths_, provision, result] {
        ToolchainManager manager(paths);
        QLockFile lock(QDir(paths.runtimeDir).filePath(".update.lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock()) { result->error = "Обновление runtime уже выполняется"; return; }
        result->status = provision ? manager.ensureRuntime() : manager.status(true);
    });
    connect(thread, &QThread::finished, this, [this, result] {
        busy_ = false;
        if (!result->error.isEmpty()) emit failed(result->error);
        else emit ready(result->status);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}
}
