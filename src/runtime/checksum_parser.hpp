#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

namespace vdp {
std::optional<QByteArray> findSha256(const QString& text, const QString& expectedFileName,
                                   bool singleArtifactDocument = false);
QByteArray fileSha256(const QString& path);
}
