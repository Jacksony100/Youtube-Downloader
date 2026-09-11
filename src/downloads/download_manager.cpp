#include "download_manager.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>
#include <QUrl>

namespace vdp {
namespace {
DownloadErrorCategory errorCategory(const QString& diagnostic) {
    const auto text = diagnostic.toLower();
    if (text.contains("javascript runtime")) return DownloadErrorCategory::RuntimeMissing;
    if (text.contains("private") || text.contains("sign in") || text.contains("login") || text.contains("authentication")) return DownloadErrorCategory::PrivateOrAuthRequired;
    if (text.contains("age-restricted") || text.contains("age restricted") || text.contains("geo") || text.contains("region")) return DownloadErrorCategory::GeoOrAgeRestricted;
    if (text.contains("unavailable") || text.contains("not available")) return DownloadErrorCategory::Unavailable;
    if (text.contains("429") || text.contains("too many requests")) return DownloadErrorCategory::RateLimited;
    if (text.contains("403") || text.contains("forbidden")) return DownloadErrorCategory::Forbidden;
    if (text.contains("disk") || text.contains("permission denied") || text.contains("no space")) return DownloadErrorCategory::Disk;
    if (text.contains("network") || text.contains("connection") || text.contains("timeout")) return DownloadErrorCategory::Network;
    return DownloadErrorCategory::Unknown;
}
}

DownloadManager::DownloadManager(const ToolchainManager* toolchain, QObject* parent)
    : QObject(parent), toolchain_(toolchain) {
    if (toolchain_) queueStore_ = std::make_unique<QueueStore>(QDir(toolchain_->paths().dataDir).filePath("queue.json"));
    processFactory_ = [](QObject* owner) { return new QProcess(owner); };
    commandBuilder_ = [this](const DownloadTaskData& task) {
        if (!toolchain_) return DownloadCommand{};
        FormatPreset preset{task.formatKey, task.formatLabel, task.formatSelector, task.extractAudio, task.extension};
        preset.audioQuality = task.audioQuality;
        preset.originalAudio = task.originalAudio;
        return DownloadCommand{toolchain_->ytdlpPath(), buildDownloadArguments(task.url, preset,
            task.outputDirectory, toolchain_->ffmpegDirectory(), toolchain_->denoPath())};
    };
}

DownloadManager::~DownloadManager() { shutdown(); }

void DownloadManager::setQueueFile(const QString& filePath) {
    if (!tasks_.isEmpty()) return;
    queueStore_ = filePath.isEmpty() ? nullptr : std::make_unique<QueueStore>(filePath);
    persistenceBlocked_ = false;
    restored_ = false;
}

void DownloadManager::setCancellationTimeout(int milliseconds) {
    cancellationTimeoutMs_ = qBound(1, milliseconds, 10000);
}

QString DownloadManager::enqueue(const DownloadRequest& request) {
    if (shuttingDown_) return {};
    const QUrl url(request.url);
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "https" && url.scheme() != "http")
        || request.outputDirectory.trimmed().isEmpty()) return {};
    auto run = std::make_shared<Run>();
    auto& data = run->data;
    data.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    data.url = request.url;
    data.title = request.title.isEmpty() ? request.url : request.title;
    data.outputDirectory = request.outputDirectory;
    data.formatKey = request.preset.key;
    data.formatLabel = request.preset.label;
    data.formatSelector = request.preset.selector;
    data.extension = request.preset.extension;
    data.audioQuality = request.preset.audioQuality;
    data.extractAudio = request.preset.extractAudio;
    data.originalAudio = request.preset.originalAudio;
    data.createdAt = data.updatedAt = QDateTime::currentDateTimeUtc();
    tasks_.insert(data.id, run);
    order_.append(data.id);
    pending_.append(data.id);
    emit taskAdded(data.id);
    persistQueue();
    notifyQueue();
    QTimer::singleShot(0, this, &DownloadManager::schedule);
    return data.id;
}

const DownloadTaskData* DownloadManager::task(const QString& taskId) const {
    const auto run = tasks_.value(taskId);
    return run ? &run->data : nullptr;
}

int DownloadManager::runningCount() const {
    int count = 0;
    for (const auto& run : tasks_) if (run->slotReserved && run->started) ++count;
    return count;
}

int DownloadManager::activeCount() const {
    int count = 0;
    for (const auto& run : tasks_) if (run->slotReserved) ++count;
    return count;
}

int DownloadManager::queuedCount() const {
    int count = static_cast<int>(pending_.size());
    for (const auto& run : tasks_) if (run->slotReserved && !run->started) ++count;
    return count;
}

void DownloadManager::notifyQueue() { emit queueChanged(runningCount(), queuedCount()); }

void DownloadManager::setParallelLimit(int limit) {
    parallelLimit_ = qBound(1, limit, 8);
    QTimer::singleShot(0, this, &DownloadManager::schedule);
}

void DownloadManager::setTitle(const QString& taskId, const QString& title) {
    const auto run = tasks_.value(taskId);
    if (!run || title.isEmpty() || run->data.title == title) return;
    run->data.title = title;
    changed(run);
}

void DownloadManager::start() { setPaused(false); }

void DownloadManager::setPaused(bool paused) {
    if (shuttingDown_) return;
    paused_ = paused;
    if (!paused_) QTimer::singleShot(0, this, &DownloadManager::schedule);
}

void DownloadManager::schedule() {
    if (paused_ || shuttingDown_ || scheduling_) return;
    scheduling_ = true;
    while (!paused_ && !shuttingDown_ && activeCount() < parallelLimit_ && !pending_.isEmpty()) {
        const auto id = pending_.takeFirst();
        const auto run = tasks_.value(id);
        if (run && !isTerminal(run->data.state)) launch(run);
    }
    scheduling_ = false;
    notifyQueue();
}

void DownloadManager::launch(const std::shared_ptr<Run>& run) {
    run->slotReserved = true;
    if (!QDir().mkpath(run->data.outputDirectory)) {
        finalize(run, TaskState::Failed, {QStringLiteral("Не удалось создать папку загрузок."),
            QStringLiteral("Output directory creation failed"), 0, DownloadErrorCategory::Disk});
        return;
    }
    auto* process = processFactory_(this);
    if (!process) {
        finalize(run, TaskState::Failed, {QStringLiteral("Не удалось запустить процесс загрузки."),
            QStringLiteral("Process factory returned no process"), 0, DownloadErrorCategory::ProcessStartFailed});
        return;
    }
    if (!process->parent()) process->setParent(this);
    run->process = process;
    connect(process, &QProcess::started, this, [this, run] {
        if (!run->slotReserved || isTerminal(run->lifecycle.state())) return;
        run->started = true;
        if (run->lifecycle.cancellationRequested()) {
            run->process->terminate();
            // Cancellation can race process startup; ensure escalation still happens after started.
            QTimer::singleShot(cancellationTimeoutMs_, this, [run] {
                if (run->process && !isTerminal(run->data.state)
                    && run->process->state() != QProcess::NotRunning) run->process->kill();
            });
        }
        else if (run->lifecycle.started()) {
            run->data.state = run->lifecycle.state();
            changed(run);
        }
        notifyQueue();
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, run] { consumeOutput(run); });
    connect(process, &QProcess::readyReadStandardError, this, [this, run] { consumeOutput(run); });
    connect(process, &QProcess::errorOccurred, this, [this, run](QProcess::ProcessError error) {
        if (!run->process || isTerminal(run->data.state)) return;
        if (error == QProcess::FailedToStart) {
            finalize(run, TaskState::Failed, {QStringLiteral("Не удалось запустить yt-dlp. Восстановите runtime в разделе «Инструменты»."),
                run->process->errorString(), 0, DownloadErrorCategory::ProcessStartFailed});
        } else if (error == QProcess::Crashed) {
            consumeOutput(run, true);
            finalize(run, TaskState::Failed, {QStringLiteral("Процесс загрузки неожиданно завершился. Повторите попытку."),
                run->process->errorString(), run->process->exitCode(), DownloadErrorCategory::Unknown});
        }
    });
    connect(process, &QProcess::finished, this, [this, run](int exitCode, QProcess::ExitStatus status) {
        if (!run->process || isTerminal(run->data.state)) return;
        consumeOutput(run, true);
        run->stderrText = (run->stderrText + QString::fromUtf8(run->process->readAllStandardError())).right(32768);
        if (run->lifecycle.cancellationRequested()) {
            finalize(run, TaskState::Cancelled);
        } else if (exitCode != 0 || status != QProcess::NormalExit) {
            finalize(run, TaskState::Failed, {sanitizeError(run->stderrText), run->stderrText,
                exitCode, errorCategory(run->stderrText)});
        } else {
            const QFileInfo output(run->data.outputPath);
            if (run->data.outputPath.isEmpty() || !output.exists() || !output.isFile()) {
                finalize(run, TaskState::Failed, {QStringLiteral("Загрузчик завершился, но итоговый файл не найден."),
                    QStringLiteral("Missing final output file after successful process exit"), exitCode, DownloadErrorCategory::Disk});
            } else finalize(run, TaskState::Completed);
        }
    });
    const auto command = commandBuilder_(run->data);
    process->start(command.program, command.arguments);
}

void DownloadManager::consumeOutput(const std::shared_ptr<Run>& run, bool flush) {
    if (!run->process || isTerminal(run->data.state)) return;
    bool updated = false;
    bool persist = false;
    auto events = run->parser.feed(run->process->readAllStandardOutput(), flush);
    const auto stderrBytes = run->process->readAllStandardError();
    run->stderrText = (run->stderrText + QString::fromUtf8(stderrBytes)).right(32768);
    events += run->stderrParser.feed(stderrBytes, flush);
    for (const auto& result : events) {
        if (result.finalPath) { run->data.outputPath = *result.finalPath; updated = persist = true; }
        if (result.progress) {
            if (result.progress->percent) run->data.progressPercent = *result.progress->percent;
            if (!result.progress->speed.isEmpty()) run->data.speed = result.progress->speed;
            if (!result.progress->eta.isEmpty()) run->data.eta = result.progress->eta;
            updated = true;
        }
        if (result.postProcessing && run->lifecycle.postProcessing()) {
            run->data.state = run->lifecycle.state();
            updated = persist = true;
        }
    }
    if (updated) changed(run, persist);
}

void DownloadManager::changed(const std::shared_ptr<Run>& run, bool persist) {
    run->data.updatedAt = QDateTime::currentDateTimeUtc();
    emit taskChanged(run->data.id);
    if (persist) persistQueue();
}

void DownloadManager::finalize(const std::shared_ptr<Run>& run, TaskState state, TaskFailure failure) {
    if (shuttingDown_ || !run->lifecycle.finalize(state)) return;
    run->data.state = run->lifecycle.state();
    if (run->data.state == TaskState::Cancelled) {
        failure = {QStringLiteral("Отменено пользователем"), {}, 0, DownloadErrorCategory::Cancelled};
    }
    run->data.failure = std::move(failure);
    run->data.errorMessage = run->data.failure.userMessage;
    if (run->data.state == TaskState::Completed) run->data.progressPercent = 100;
    run->slotReserved = false;
    run->started = false;
    pending_.removeAll(run->data.id);
    if (auto* process = run->process.data()) {
        disconnect(process, nullptr, this, nullptr);
        if (process->state() == QProcess::NotRunning) process->deleteLater();
        else {
            connect(process, &QProcess::finished, process, &QObject::deleteLater);
            process->kill();
        }
        run->process = nullptr;
    }
    changed(run);
    emit taskFinished(run->data.id);
    notifyQueue();
    QTimer::singleShot(0, this, &DownloadManager::schedule);
}

void DownloadManager::cancel(const QString& taskId) {
    const auto run = tasks_.value(taskId);
    if (!run || !run->lifecycle.requestCancellation()) return;
    run->data.cancellationRequested = true;
    pending_.removeAll(taskId);
    persistQueue();
    if (!run->process || run->process->state() == QProcess::NotRunning) {
        finalize(run, TaskState::Cancelled);
        return;
    }
    run->process->terminate();
    QTimer::singleShot(cancellationTimeoutMs_, this, [this, run] {
        if (shuttingDown_ || !run->process || isTerminal(run->data.state)) return;
        if (run->process->state() != QProcess::NotRunning) run->process->kill();
        else finalize(run, TaskState::Cancelled);
    });
    changed(run, false);
    notifyQueue();
}

void DownloadManager::retry(const QString& taskId) {
    const auto previous = tasks_.value(taskId);
    if (!previous || (previous->data.state != TaskState::Failed && previous->data.state != TaskState::Cancelled)
        || shuttingDown_) return;
    auto run = std::make_shared<Run>();
    run->data = previous->data;
    run->data.state = TaskState::Queued;
    run->data.cancellationRequested = false;
    run->data.recovered = false;
    run->data.progressPercent = 0;
    run->data.outputPath.clear();
    run->data.speed.clear();
    run->data.eta.clear();
    run->data.errorMessage.clear();
    run->data.failure = {};
    tasks_.insert(taskId, run);
    pending_.append(taskId);
    changed(run);
    notifyQueue();
    QTimer::singleShot(0, this, &DownloadManager::schedule);
}

void DownloadManager::removeTerminal(const QString& taskId) {
    const auto run = tasks_.value(taskId);
    if (!run || !isTerminal(run->data.state)) return;
    tasks_.remove(taskId);
    order_.removeAll(taskId);
    emit taskRemoved(taskId);
}

void DownloadManager::persistQueue() {
    if (!queueStore_ || persistenceBlocked_) return;
    QVector<DownloadTaskData> tasks;
    // Creation order is stable, including running items that must survive shutdown.
    for (const auto& id : order_) if (const auto run = tasks_.value(id)) tasks.append(run->data);
    QString error;
    if (!queueStore_->save(tasks, &error)) emit storageError(error);
}

int DownloadManager::restoreQueue() {
    if (restored_ || !queueStore_ || shuttingDown_) return 0;
    restored_ = true;
    QString error;
    const auto recovered = queueStore_->load(&error);
    if (!error.isEmpty()) {
        const auto original = queueStore_->filePath();
        const auto backup = original + ".recovery-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".json";
        if (!QFile::copy(original, backup)) persistenceBlocked_ = true;
        emit storageError(error + (persistenceBlocked_
            ? QStringLiteral(" Автосохранение отключено, чтобы сохранить исходный файл.")
            : QStringLiteral(" Исходный файл сохранён: %1").arg(backup)));
    }
    int count = 0;
    for (const auto& data : recovered) {
        if (tasks_.contains(data.id)) continue;
        auto run = std::make_shared<Run>();
        run->data = data;
        tasks_.insert(data.id, run);
        order_.append(data.id);
        pending_.append(data.id);
        emit taskAdded(data.id);
        ++count;
    }
    notifyQueue();
    return count;
}

void DownloadManager::shutdown() {
    if (shuttingDown_) return;
    paused_ = true;
    persistQueue();
    shuttingDown_ = true;
    QList<QProcess*> processes;
    for (const auto& run : tasks_) {
        if (auto* process = run->process.data()) {
            disconnect(process, nullptr, this, nullptr);
            processes.append(process);
            if (process->state() != QProcess::NotRunning) process->terminate();
        }
    }
    for (auto* process : processes) {
        if (process->state() != QProcess::NotRunning && !process->waitForFinished(100)) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
}

} // namespace vdp
