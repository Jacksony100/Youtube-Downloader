#pragma once
#include "core.hpp"
#include <QMetaType>

namespace vdp {
struct FormatOption {
    QString id, label, selector, extension, videoCodec, audioCodec;
    int height = 0;
    double fps = 0;
    qint64 estimatedBytes = -1;
    bool audioOnly = false;
    FormatPreset preset;
};
struct VideoMetadata {
    QString title, uploader, thumbnailUrl, sourceHost;
    int durationSeconds = 0;
    QVector<FormatOption> formats;
};
}
Q_DECLARE_METATYPE(vdp::VideoMetadata)
