#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <optional>

namespace vdp {
struct ToolBinary {
    QString name;
    QString target;
    QStringList versionArgs;
};
struct ToolArtifact {
    QUrl url;
    QString fileName;
    QUrl checksumUrl;
    QByteArray sha256;
    bool singleArtifactChecksum = false;
    bool archive = false;
    QVector<ToolBinary> binaries;
};
struct ToolUpdateSpec {
    QString toolId;
    QVector<ToolArtifact> artifacts;
};
QString toolchainPlatform();
std::optional<ToolUpdateSpec> parseToolchainLock(const QByteArray& json, const QString& key, QString* error = nullptr);
std::optional<ToolUpdateSpec> loadToolchainSpec(const QString& toolId, QString* error = nullptr);
bool validateToolUpdateSpec(const ToolUpdateSpec& spec, QString* error = nullptr);
}
