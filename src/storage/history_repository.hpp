#pragma once

#include "downloads/download_task.hpp"
#include <QVector>

namespace vdp {

struct HistoryRecord {
    DownloadTaskData task;
    QDateTime completedAt;
};

class HistoryRepository {
public:
    static constexpr int maximumRecords = 500;
    explicit HistoryRepository(QString filePath) : filePath_(std::move(filePath)) {}
    bool load(QString* error = nullptr);
    bool append(const DownloadTaskData& task, QString* error = nullptr);
    QVector<HistoryRecord> newestFirst(const QString& query = {}) const;
    bool remove(const QString& id, QString* error = nullptr);
    bool clear(QString* error = nullptr);

private:
    QString filePath_;
    QVector<HistoryRecord> records_;
    bool loaded_ = false;
    bool save(const QVector<HistoryRecord>& records, QString* error) const;
};

} // namespace vdp
