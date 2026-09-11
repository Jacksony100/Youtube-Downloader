#include "history_repository.hpp"
#include "task_serialization.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

namespace vdp {

bool HistoryRepository::load(QString* error) {
    if (error) error->clear();
    QFile file(filePath_);
    if (!file.exists()) { records_.clear(); loaded_ = true; return true; }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Не удалось прочитать историю: %1").arg(file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const bool legacy = document.isArray();
    if (parseError.error != QJsonParseError::NoError || (!legacy
        && (!document.isObject() || document.object().value("schema").toInt(-1) != 1
            || !document.object().value("records").isArray()))) {
        if (error) *error = QStringLiteral("История повреждена или версия формата не поддерживается. Исходный файл сохранён.");
        return false;
    }
    QVector<HistoryRecord> records;
    const auto array = legacy ? document.array() : document.object().value("records").toArray();
    for (const auto& value : array) {
        const auto object = value.toObject();
        auto task = storage::taskFromJson(object, true);
        if (!task || !isTerminal(task->state)) {
            if (error) *error = QStringLiteral("В истории есть повреждённая запись. Исходный файл сохранён.");
            return false;
        }
        auto completed = QDateTime::fromString(object.value("completed_at").toString(), Qt::ISODateWithMs).toUTC();
        if (!completed.isValid()) completed = task->updatedAt;
        records.append({*task, completed});
    }
    std::stable_sort(records.begin(), records.end(), [](const auto& first, const auto& second) {
        return first.completedAt > second.completedAt;
    });
    if (records.size() > maximumRecords) records.resize(maximumRecords);
    records_ = std::move(records);
    loaded_ = true;
    return true;
}

bool HistoryRepository::save(const QVector<HistoryRecord>& records, QString* error) const {
    QJsonArray array;
    for (const auto& record : records) {
        auto object = storage::taskToJson(record.task);
        object.insert("completed_at", record.completedAt.toUTC().toString(Qt::ISODateWithMs));
        array.append(object);
    }
    return storage::writeJsonAtomically(filePath_, QJsonDocument(QJsonObject{
        {"schema", 1}, {"records", array}}).toJson(), error);
}

bool HistoryRepository::append(const DownloadTaskData& task, QString* error) {
    if (error) error->clear();
    if (!isTerminal(task.state)) {
        if (error) *error = QStringLiteral("В историю можно добавить только завершённую задачу.");
        return false;
    }
    if (!loaded_ && !load(error)) return false;
    auto records = records_;
    records.erase(std::remove_if(records.begin(), records.end(), [&task](const auto& record) {
        return record.task.id == task.id;
    }), records.end());
    records.prepend({task, task.updatedAt.isValid() ? task.updatedAt : QDateTime::currentDateTimeUtc()});
    std::stable_sort(records.begin(), records.end(), [](const auto& first, const auto& second) {
        return first.completedAt > second.completedAt;
    });
    if (records.size() > maximumRecords) records.resize(maximumRecords);
    if (!save(records, error)) return false;
    records_ = std::move(records);
    return true;
}

QVector<HistoryRecord> HistoryRepository::newestFirst(const QString& query) const {
    QVector<HistoryRecord> result;
    for (const auto& record : records_) {
        const auto haystack = record.task.title + ' ' + record.task.url + ' ' + record.task.formatLabel;
        if (query.isEmpty() || haystack.contains(query, Qt::CaseInsensitive)) result.append(record);
    }
    return result;
}

bool HistoryRepository::remove(const QString& id, QString* error) {
    if (!loaded_ && !load(error)) return false;
    auto records = records_;
    records.erase(std::remove_if(records.begin(), records.end(), [&id](const auto& record) {
        return record.task.id == id;
    }), records.end());
    if (!save(records, error)) return false;
    records_ = std::move(records);
    return true;
}

bool HistoryRepository::clear(QString* error) {
    if (!save({}, error)) return false;
    records_.clear();
    loaded_ = true;
    return true;
}

} // namespace vdp
