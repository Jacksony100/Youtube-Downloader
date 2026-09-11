#include "download_task.hpp"

namespace vdp {

bool isTerminal(TaskState state) {
    return state == TaskState::Completed || state == TaskState::Cancelled || state == TaskState::Failed;
}

QString taskStateKey(TaskState state) {
    switch (state) {
    case TaskState::Queued: return QStringLiteral("queued");
    case TaskState::Preparing: return QStringLiteral("preparing");
    case TaskState::Downloading: return QStringLiteral("downloading");
    case TaskState::PostProcessing: return QStringLiteral("postprocessing");
    case TaskState::Completed: return QStringLiteral("completed");
    case TaskState::Cancelled: return QStringLiteral("cancelled");
    case TaskState::Failed: return QStringLiteral("failed");
    }
    return {};
}

QString taskStateLabel(TaskState state) {
    switch (state) {
    case TaskState::Queued: return QStringLiteral("В очереди");
    case TaskState::Preparing: return QStringLiteral("Подготовка...");
    case TaskState::Downloading: return QStringLiteral("Загрузка...");
    case TaskState::PostProcessing: return QStringLiteral("Обработка...");
    case TaskState::Completed: return QStringLiteral("Загрузка завершена");
    case TaskState::Cancelled: return QStringLiteral("Отменено");
    case TaskState::Failed: return QStringLiteral("Ошибка");
    }
    return {};
}

std::optional<TaskState> taskStateFromKey(const QString& key) {
    for (TaskState state : {TaskState::Queued, TaskState::Preparing, TaskState::Downloading,
             TaskState::PostProcessing, TaskState::Completed, TaskState::Cancelled, TaskState::Failed}) {
        if (taskStateKey(state) == key) return state;
    }
    return std::nullopt;
}

bool TaskLifecycle::started() {
    if (isTerminal(state_) || cancellationRequested_
        || (state_ != TaskState::Queued && state_ != TaskState::Preparing)) return false;
    state_ = TaskState::Downloading;
    return true;
}

bool TaskLifecycle::postProcessing() {
    if (state_ != TaskState::Downloading || cancellationRequested_) return false;
    state_ = TaskState::PostProcessing;
    return true;
}

bool TaskLifecycle::requestCancellation() {
    if (isTerminal(state_) || cancellationRequested_) return false;
    cancellationRequested_ = true;
    return true;
}

bool TaskLifecycle::finalize(TaskState intendedState) {
    if (isTerminal(state_) || !isTerminal(intendedState)) return false;
    if (intendedState == TaskState::Completed
        && state_ != TaskState::Downloading && state_ != TaskState::PostProcessing) return false;
    state_ = cancellationRequested_ ? TaskState::Cancelled : intendedState;
    return true;
}

bool TaskLifecycle::finish(int exitCode, bool normalExit) {
    return finalize(cancellationRequested_ ? TaskState::Cancelled
        : exitCode == 0 && normalExit ? TaskState::Completed : TaskState::Failed);
}

} // namespace vdp
