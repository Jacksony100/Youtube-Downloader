#include "task_serialization.hpp"
#include "core.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>

namespace vdp::storage {

QJsonObject taskToJson(const DownloadTaskData& task) {
    return {{"id", task.id}, {"url", task.url}, {"title", task.title},
        {"output_directory", task.outputDirectory}, {"path", task.outputPath},
        {"format_key", task.formatKey}, {"format", task.formatLabel},
        {"format_selector", task.formatSelector}, {"extension", task.extension},
        {"extract_audio", task.extractAudio}, {"audio_quality", task.audioQuality},
        {"original_audio", task.originalAudio},
        {"status", taskStateKey(task.state)}, {"error", task.errorMessage},
        {"created_at", task.createdAt.toUTC().toString(Qt::ISODateWithMs)},
        {"updated_at", task.updatedAt.toUTC().toString(Qt::ISODateWithMs)}};
}

std::optional<DownloadTaskData> taskFromJson(const QJsonObject& object, bool legacyHistory) {
    DownloadTaskData task;
    task.url = object.value("url").toString();
    const QUrl url(task.url);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != "https" && url.scheme() != "http")) return std::nullopt;
    task.id = object.value("id").toString();
    if (task.id.isEmpty()) {
        if (!legacyHistory) return std::nullopt;
        task.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    const auto state = taskStateFromKey(object.value("status").toString());
    if (!state) return std::nullopt;
    task.state = *state;
    task.title = object.value("title").toString(task.url);
    task.outputPath = object.value("path").toString();
    task.outputDirectory = object.value("output_directory").toString();
    if (task.outputDirectory.isEmpty() && !task.outputPath.isEmpty())
        task.outputDirectory = QFileInfo(task.outputPath).absolutePath();
    if (task.outputDirectory.isEmpty() && !legacyHistory) return std::nullopt;
    task.formatLabel = object.value("format").toString();
    task.formatKey = object.value("format_key").toString();
    if (task.formatKey.isEmpty()) {
        for (const auto& preset : formatPresets()) {
            if (preset.label == task.formatLabel || preset.key == task.formatLabel) {
                task.formatKey = preset.key;
                break;
            }
        }
    }
    const auto preset = formatPreset(task.formatKey);
    if (task.formatKey.isEmpty()) task.formatKey = preset.key;
    if (task.formatLabel.isEmpty()) task.formatLabel = preset.label;
    task.formatSelector = object.value("format_selector").toString(preset.selector);
    task.extension = object.value("extension").toString(preset.extension);
    task.extractAudio = object.value("extract_audio").toBool(preset.extractAudio);
    task.originalAudio = object.value("original_audio").toBool(preset.originalAudio);
    task.audioQuality = object.value("audio_quality").toString(preset.audioQuality);
    if (task.audioQuality.trimmed().isEmpty()) task.audioQuality = preset.audioQuality;
    task.errorMessage = object.value("error").toString();
    task.createdAt = QDateTime::fromString(object.value("created_at").toString(), Qt::ISODateWithMs).toUTC();
    task.updatedAt = QDateTime::fromString(object.value("updated_at").toString(), Qt::ISODateWithMs).toUTC();
    if (!task.createdAt.isValid()) task.createdAt = QDateTime::currentDateTimeUtc();
    if (!task.updatedAt.isValid()) task.updatedAt = task.createdAt;
    return task;
}

bool writeJsonAtomically(const QString& path, const QByteArray& data, QString* error) {
    if (error) error->clear();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = QStringLiteral("Не удалось создать папку данных: %1").arg(QFileInfo(path).absolutePath());
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        if (error) *error = QStringLiteral("Не удалось атомарно сохранить данные: %1").arg(file.errorString());
        return false;
    }
    return true;
}

} // namespace vdp::storage
