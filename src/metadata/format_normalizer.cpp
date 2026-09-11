#include "format_normalizer.hpp"
#include <QHash>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>

namespace vdp {
namespace {
QString codecFamily(const QString& codec) {
    if (codec.startsWith("avc") || codec.startsWith("h264")) return "H.264";
    if (codec.startsWith("av01")) return "AV1";
    if (codec.startsWith("vp9") || codec.startsWith("vp09")) return "VP9";
    return codec.section('.', 0, 0).toUpper();
}
bool hasCodec(const QString& codec) { return !codec.isEmpty() && codec != "none"; }
qint64 estimatedSize(const QJsonObject& row) {
    const auto exact = row.value("filesize").toInteger(-1);
    return exact > 0 ? exact : row.value("filesize_approx").toInteger(-1);
}
}
QVector<FormatOption> normalizeFormats(const QJsonArray& formats) {
    QHash<QString, FormatOption> video;
    bool hasAudio = false;
    for (const auto& value : formats) {
        const auto row = value.toObject();
        const QString id = row.value("format_id").toString();
        static const QRegularExpression safeId("^[A-Za-z0-9_.-]+$");
        if (!safeId.match(id).hasMatch() || row.value("has_drm").toBool()) continue;
        const QString vc = row.value("vcodec").toString();
        const QString ac = row.value("acodec").toString();
        hasAudio |= hasCodec(ac);
        if (!hasCodec(vc)) continue;
        const int height = row.value("height").toInt();
        if (height <= 0) continue;
        const double fps = row.value("fps").toDouble();
        const QString codec = codecFamily(vc);
        const QString key = QString("%1/%2/%3").arg(height).arg(qRound(fps)).arg(codec);
        FormatOption option;
        option.id = id; option.height = height; option.fps = fps;
        option.videoCodec = vc; option.audioCodec = ac;
        option.extension = codec == "H.264" ? "mp4" : "mkv";
        option.selector = hasCodec(ac) ? id : id + "+bestaudio/best";
        // Video-only size excludes the audio selected at download time.
        option.estimatedBytes = estimatedSize(row);
        option.label = QString("%1p • %2").arg(height).arg(codec);
        if (fps > 30) option.label += QString(" • %1 fps").arg(qRound(fps));
        if (option.estimatedBytes > 0) option.label += QString(" • ~%1 МБ%2")
            .arg(option.estimatedBytes / (1024.0 * 1024.0), 0, 'f', 1)
            .arg(hasCodec(ac) ? "" : " (видео)");
        option.preset = {"advanced:" + id, option.label, option.selector, false, option.extension};
        auto previous = video.constFind(key);
        if (previous == video.cend() || option.estimatedBytes > previous->estimatedBytes) video.insert(key, option);
    }
    QVector<FormatOption> result(video.cbegin(), video.cend());
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.height != b.height) return a.height > b.height;
        if (a.fps != b.fps) return a.fps > b.fps;
        return a.label < b.label;
    });
    if (result.size() > 24) result.resize(24);
    if (hasAudio) {
        FormatOption original;
        original.id = "original-audio"; original.label = "Аудио • оригинал / лучшее";
        original.selector = "bestaudio/best"; original.audioOnly = true;
        original.preset = {"original-audio", original.label, original.selector, true, "best"};
        original.preset.originalAudio = true;
        result.append(original);
        for (const auto& bitrate : {QString("192K"), QString("320K")}) {
            FormatOption mp3;
            mp3.id = "mp3-" + bitrate; mp3.label = "MP3 • целевой битрейт " + bitrate;
            mp3.selector = "bestaudio/best"; mp3.extension = "mp3"; mp3.audioOnly = true;
            mp3.preset = {mp3.id, mp3.label, mp3.selector, true, "mp3"};
            mp3.preset.audioQuality = bitrate;
            result.append(mp3);
        }
    }
    return result;
}
VideoMetadata parseMetadata(const QJsonObject& object, const QString& url) {
    VideoMetadata metadata;
    metadata.title = object.value("title").toString("Без названия");
    metadata.uploader = object.value("uploader").toString("Автор неизвестен");
    metadata.durationSeconds = qMax(0, object.value("duration").toInt());
    metadata.sourceHost = QUrl(url).host();
    metadata.thumbnailUrl = object.value("thumbnail").toString();
    const auto supported = [](const QString& candidate) {
        const auto path = QUrl(candidate).path().toLower();
        return path.endsWith(".jpg") || path.endsWith(".jpeg") || path.endsWith(".png");
    };
    if (!supported(metadata.thumbnailUrl)) {
        const auto thumbnails = object.value("thumbnails").toArray();
        for (qsizetype i = thumbnails.size(); i > 0; --i) {
            const auto candidate = thumbnails[i - 1].toObject().value("url").toString();
            if (supported(candidate)) { metadata.thumbnailUrl = candidate; break; }
        }
    }
    metadata.formats = normalizeFormats(object.value("formats").toArray());
    return metadata;
}
}
