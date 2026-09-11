#include "queue_store.hpp"
#include "task_serialization.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace vdp {

bool QueueStore::save(const QVector<DownloadTaskData>& tasks, QString* error) const {
    QJsonArray array;
    for (const auto& task : tasks) {
        if (!isTerminal(task.state) && !task.cancellationRequested) array.append(storage::taskToJson(task));
    }
    return storage::writeJsonAtomically(filePath_, QJsonDocument(QJsonObject{
        {"schema", 1}, {"tasks", array}}).toJson(), error);
}

QVector<DownloadTaskData> QueueStore::load(QString* error) const {
    if (error) error->clear();
    QVector<DownloadTaskData> tasks;
    QFile file(filePath_);
    if (!file.exists()) return tasks;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Не удалось прочитать очередь: %1").arg(file.errorString());
        return tasks;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value("schema").toInt(-1) != 1
        || !document.object().value("tasks").isArray()) {
        if (error) *error = QStringLiteral("Не удалось восстановить очередь: файл повреждён или версия формата не поддерживается.");
        return tasks;
    }
    QSet<QString> ids;
    for (const auto& value : document.object().value("tasks").toArray()) {
        auto task = storage::taskFromJson(value.toObject());
        if (!task || ids.contains(task->id)) {
            if (error) *error = QStringLiteral("Некоторые записи очереди повреждены и пропущены.");
            continue;
        }
        ids.insert(task->id);
        if (isTerminal(task->state)) continue;
        task->recovered = task->state != TaskState::Queued;
        task->state = TaskState::Queued;
        task->progressPercent = 0;
        tasks.append(*task);
    }
    return tasks;
}

} // namespace vdp
