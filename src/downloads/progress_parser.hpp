#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <optional>

namespace vdp {

struct DownloadProgress {
    std::optional<double> percent;
    QString speed;
    QString eta;
    std::optional<qint64> downloadedBytes;
    std::optional<qint64> totalBytes;
};

struct ProgressParseResult {
    std::optional<DownloadProgress> progress;
    std::optional<QString> finalPath;
    bool postProcessing = false;
};

class ProgressParser {
public:
    static ProgressParseResult parseLine(const QString& line);
    QVector<ProgressParseResult> feed(const QByteArray& bytes, bool flush = false);
    void reset();

private:
    QByteArray buffer_;
    bool discardingOversizedLine_ = false;
};

} // namespace vdp
