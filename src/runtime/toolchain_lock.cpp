#include "toolchain_lock.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>

namespace vdp {
QString toolchainPlatform() {
    const QString arch = QSysInfo::currentCpuArchitecture().contains("arm") ? "arm64" : "x64";
#ifdef Q_OS_WIN
    return "windows-" + arch;
#elif defined(Q_OS_MACOS)
    return "macos-" + arch;
#else
    return "linux-" + arch;
#endif
}

bool validateToolUpdateSpec(const ToolUpdateSpec& spec, QString* error) {
    auto fail = [&](const QString& message) { if (error) *error = message; return false; };
    if (spec.toolId.isEmpty() || spec.artifacts.isEmpty()) return fail("Пустое описание runtime");
    const QSet<QString> knownTargets{"yt-dlp", "deno", "ffmpeg", "ffprobe"};
    QSet<QString> seenTargets;
    static const QRegularExpression hash("^[0-9a-fA-F]{64}$");
    auto safeName = [](const QString& name) {
        return !name.isEmpty() && name != "." && name != ".." && !name.contains('/') &&
               !name.contains('\\') && !name.contains(':') && !name.contains(QChar::Null);
    };
    for (const auto& artifact : spec.artifacts) {
        if (!artifact.url.isValid() || artifact.url.scheme() != "https" || artifact.url.host().isEmpty() ||
            !artifact.url.userInfo().isEmpty()) return fail("Runtime URL должен использовать HTTPS");
        if (!safeName(artifact.fileName)) return fail("Некорректное имя архива runtime");
        if (artifact.sha256.isEmpty()) {
            if (!artifact.checksumUrl.isValid() || artifact.checksumUrl.scheme() != "https" ||
                artifact.checksumUrl.host().isEmpty() || !artifact.checksumUrl.userInfo().isEmpty())
                return fail("Отсутствует HTTPS-источник SHA256");
        } else if (!hash.match(QString::fromLatin1(artifact.sha256)).hasMatch())
            return fail("Некорректная SHA256 фиксация runtime");
        if (artifact.binaries.isEmpty() || (!artifact.archive && artifact.binaries.size() != 1))
            return fail("Некорректный список файлов runtime");
        for (const auto& binary : artifact.binaries) {
            if (!safeName(binary.name) || !knownTargets.contains(binary.target) || seenTargets.contains(binary.target) ||
                binary.versionArgs.isEmpty()) return fail("Некорректный файл runtime");
            seenTargets.insert(binary.target);
        }
    }
    if (spec.toolId == "ffmpeg" && (!seenTargets.contains("ffmpeg") || !seenTargets.contains("ffprobe")))
        return fail("FFmpeg и ffprobe должны обновляться вместе");
    return true;
}

std::optional<ToolUpdateSpec> parseToolchainLock(const QByteArray& json, const QString& key, QString* error) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(json, &parseError);
    const auto root = document.object();
    if (parseError.error != QJsonParseError::NoError || root.value("schema").toInt() != 1) {
        if (error) *error = "Неподдерживаемый формат toolchain-lock";
        return std::nullopt;
    }
    const auto entry = root.value("tools").toObject().value(key).toObject();
    ToolUpdateSpec spec{entry.value("toolId").toString(), {}};
    for (const auto& item : entry.value("artifacts").toArray()) {
        const auto value = item.toObject();
        ToolArtifact artifact;
        artifact.url = QUrl(value.value("url").toString());
        artifact.fileName = value.value("fileName").toString();
        artifact.checksumUrl = QUrl(value.value("checksumUrl").toString());
        artifact.sha256 = value.value("sha256").toString().toLatin1().toLower();
        artifact.singleArtifactChecksum = value.value("singleArtifactChecksum").toBool();
        artifact.archive = value.value("archive").toBool();
        for (const auto& itemBinary : value.value("binaries").toArray()) {
            const auto binary = itemBinary.toObject();
            QStringList args;
            for (const auto& arg : binary.value("versionArgs").toArray()) args.append(arg.toString());
            artifact.binaries.append({binary.value("name").toString(), binary.value("target").toString(), args});
        }
        spec.artifacts.append(artifact);
    }
    if (!validateToolUpdateSpec(spec, error)) return std::nullopt;
    return spec;
}

std::optional<ToolUpdateSpec> loadToolchainSpec(const QString& toolId, QString* error) {
    QFile file(":/runtime/toolchain-lock.json");
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = "Встроенный toolchain-lock не найден";
        return std::nullopt;
    }
    return parseToolchainLock(file.readAll(), toolId + "-" + toolchainPlatform(), error);
}
}
