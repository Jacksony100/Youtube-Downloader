#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <optional>

namespace vdp {

enum class TaskState { Queued, Preparing, Downloading, PostProcessing, Completed, Cancelled, Failed };
enum class DownloadErrorCategory {
    None, RuntimeMissing, ProcessStartFailed, Network, RateLimited, Forbidden,
    PrivateOrAuthRequired, Unavailable, GeoOrAgeRestricted, Disk, Cancelled, Unknown
};

bool isTerminal(TaskState state);
QString taskStateKey(TaskState state);
QString taskStateLabel(TaskState state);
std::optional<TaskState> taskStateFromKey(const QString& key);

struct TaskFailure {
    QString userMessage;
    QString technicalMessage;
    int processExitCode = 0;
    DownloadErrorCategory category = DownloadErrorCategory::None;
};

struct DownloadTaskData {
    QString id;
    QString url;
    QString title;
    QString outputDirectory;
    QString outputPath;
    QString formatKey;
    QString formatLabel;
    QString formatSelector;
    QString extension = QStringLiteral("mp4");
    QString audioQuality;
    bool extractAudio = false;
    bool originalAudio = false;
    bool recovered = false;
    bool cancellationRequested = false;
    TaskState state = TaskState::Queued;
    double progressPercent = 0.0;
    QString speed;
    QString eta;
    QString errorMessage;
    TaskFailure failure;
    QDateTime createdAt;
    QDateTime updatedAt;
};

// This guard belongs to one process attempt. Retry creates a fresh guard.
class TaskLifecycle {
public:
    explicit TaskLifecycle(TaskState state = TaskState::Queued) : state_(state) {}
    TaskState state() const { return state_; }
    bool cancellationRequested() const { return cancellationRequested_; }
    bool started();
    bool postProcessing();
    bool requestCancellation();
    bool finalize(TaskState intendedState);
    bool finish(int exitCode, bool normalExit = true);

private:
    TaskState state_;
    bool cancellationRequested_ = false;
};

} // namespace vdp

Q_DECLARE_METATYPE(vdp::TaskState)
Q_DECLARE_METATYPE(vdp::DownloadTaskData)
