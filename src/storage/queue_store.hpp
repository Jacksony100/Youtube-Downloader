#pragma once

#include "downloads/download_task.hpp"
#include <QVector>

namespace vdp {

class QueueStore {
public:
    explicit QueueStore(QString filePath) : filePath_(std::move(filePath)) {}
    bool save(const QVector<DownloadTaskData>& tasks, QString* error = nullptr) const;
    QVector<DownloadTaskData> load(QString* error = nullptr) const;
    const QString& filePath() const { return filePath_; }

private:
    QString filePath_;
};

} // namespace vdp
