#include "progress_parser.hpp"

#include <cmath>

namespace vdp {
namespace {
std::optional<qint64> byteCount(const QString& value) {
    bool ok = false;
    const auto bytes = value.trimmed().toLongLong(&ok);
    return ok && bytes >= 0 ? std::optional<qint64>(bytes) : std::nullopt;
}
}

ProgressParseResult ProgressParser::parseLine(const QString& raw) {
    ProgressParseResult result;
    const auto line = raw.trimmed();
    if (line.startsWith(QStringLiteral("vdppath:"))) {
        const auto path = line.mid(8).trimmed();
        if (!path.isEmpty() && !path.contains(QChar::Null)) result.finalPath = path;
    } else if (line.startsWith(QStringLiteral("download:"))) {
        const auto fields = line.mid(9).split('|');
        QString value = fields.value(0).trimmed();
        if (value.endsWith('%')) value.chop(1);
        bool ok = false;
        const double percent = value.trimmed().toDouble(&ok);
        DownloadProgress progress;
        if (ok && std::isfinite(percent) && percent >= 0 && percent <= 100) progress.percent = percent;
        progress.speed = fields.value(1).trimmed();
        progress.eta = fields.value(2).trimmed();
        progress.downloadedBytes = byteCount(fields.value(3));
        progress.totalBytes = byteCount(fields.value(4));
        if (progress.percent || progress.downloadedBytes || progress.totalBytes) result.progress = progress;
    } else if (line.startsWith(QStringLiteral("postprocess:"))
        || line.startsWith(QStringLiteral("[Merger]"))
        || line.startsWith(QStringLiteral("[ExtractAudio]"))
        || line.startsWith(QStringLiteral("[VideoRemuxer]"))
        || line.startsWith(QStringLiteral("[Fixup"))) {
        result.postProcessing = true;
    }
    return result;
}

QVector<ProgressParseResult> ProgressParser::feed(const QByteArray& bytes, bool flush) {
    QVector<ProgressParseResult> result;
    // Bound memory even when a child produces a corrupt stream without newlines.
    constexpr qsizetype maximumLineBytes = 64 * 1024;
    qsizetype offset = 0;
    while (offset < bytes.size()) {
        qsizetype end = bytes.indexOf('\n', offset);
        const bool complete = end >= 0;
        if (!complete) end = bytes.size();
        const auto length = end - offset;
        if (!discardingOversizedLine_) {
            if (buffer_.size() + length <= maximumLineBytes) buffer_.append(bytes.constData() + offset, length);
            else { buffer_.clear(); discardingOversizedLine_ = true; }
        }
        if (complete) {
            if (!discardingOversizedLine_) result.append(parseLine(QString::fromUtf8(buffer_)));
            buffer_.clear();
            discardingOversizedLine_ = false;
        }
        offset = end + (complete ? 1 : 0);
    }
    if (flush) {
        if (!buffer_.isEmpty() && !discardingOversizedLine_) result.append(parseLine(QString::fromUtf8(buffer_)));
        reset();
    }
    return result;
}

void ProgressParser::reset() { buffer_.clear(); discardingOversizedLine_ = false; }

} // namespace vdp
