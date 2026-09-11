#pragma once

#include "core.hpp"
#include "download_task.hpp"
#include "progress_parser.hpp"
#include "storage/queue_store.hpp"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <functional>
#include <memory>

namespace vdp {

struct DownloadRequest {
    QString url;
    QString title;
    QString outputDirectory;
    FormatPreset preset;
};

struct DownloadCommand {
    QString program;
    QStringList arguments;
};

class DownloadManager final : public QObject {
    Q_OBJECT
public:
    using CommandBuilder = std::function<DownloadCommand(const DownloadTaskData&)>;
    using ProcessFactory = std::function<QProcess*(QObject*)>;
    explicit DownloadManager(const ToolchainManager* toolchain, QObject* parent = nullptr);
    ~DownloadManager() override;

    QString enqueue(const DownloadRequest& request);
    void cancel(const QString& taskId);
    void retry(const QString& taskId);
    void removeTerminal(const QString& taskId);
    void setTitle(const QString& taskId, const QString& title);
    void setParallelLimit(int limit);
    const DownloadTaskData* task(const QString& taskId) const;
    QStringList taskIds() const { return order_; }
    int runningCount() const;
    int queuedCount() const;
    int activeCount() const;
    int restoreQueue();
    void start();
    void setPaused(bool paused);
    bool isPaused() const { return paused_; }
    void shutdown();

    // Configure these before starting work; they permit deterministic local process fixtures.
    void setCommandBuilder(CommandBuilder builder) { commandBuilder_ = std::move(builder); }
    void setProcessFactory(ProcessFactory factory) { processFactory_ = std::move(factory); }
    void setQueueFile(const QString& filePath);
    void setCancellationTimeout(int milliseconds);

signals:
    void taskAdded(const QString& taskId);
    void taskChanged(const QString& taskId);
    void taskRemoved(const QString& taskId);
    void taskFinished(const QString& taskId);
    void queueChanged(int running, int queued);
    void storageError(const QString& message);

private:
    struct Run {
        DownloadTaskData data;
        TaskLifecycle lifecycle;
        ProgressParser parser;
        ProgressParser stderrParser;
        QPointer<QProcess> process;
        QString stderrText;
        bool slotReserved = false;
        bool started = false;
    };
    const ToolchainManager* toolchain_;
    std::unique_ptr<QueueStore> queueStore_;
    QHash<QString, std::shared_ptr<Run>> tasks_;
    QStringList order_;
    QStringList pending_;
    CommandBuilder commandBuilder_;
    ProcessFactory processFactory_;
    int parallelLimit_ = 2;
    int cancellationTimeoutMs_ = 1500;
    bool paused_ = true;
    bool shuttingDown_ = false;
    bool restored_ = false;
    bool persistenceBlocked_ = false;
    bool scheduling_ = false;

    void schedule();
    void launch(const std::shared_ptr<Run>& run);
    void consumeOutput(const std::shared_ptr<Run>& run, bool flush = false);
    void finalize(const std::shared_ptr<Run>& run, TaskState state, TaskFailure failure = {});
    void changed(const std::shared_ptr<Run>& run, bool persist = true);
    void persistQueue();
    void notifyQueue();
};

} // namespace vdp
