#pragma once

#include "downloads/download_task.hpp"
#include <QJsonObject>

namespace vdp::storage {
QJsonObject taskToJson(const DownloadTaskData& task);
std::optional<DownloadTaskData> taskFromJson(const QJsonObject& object, bool legacyHistory = false);
bool writeJsonAtomically(const QString& path, const QByteArray& data, QString* error);
} // namespace vdp::storage
