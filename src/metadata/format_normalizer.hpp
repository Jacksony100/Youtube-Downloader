#pragma once
#include "metadata_models.hpp"
#include <QJsonArray>

namespace vdp {
QVector<FormatOption> normalizeFormats(const QJsonArray& formats);
VideoMetadata parseMetadata(const QJsonObject& object, const QString& url);
}
