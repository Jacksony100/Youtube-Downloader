#include "core.hpp"
#include "runtime/checksum_parser.hpp"
#include "runtime/tool_install.hpp"
#include "diagnostics/diagnostics.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QUrl>
#include <QVersionNumber>

namespace vdp {

const QVector<FormatPreset>& formatPresets() {
    static const QVector<FormatPreset> presets = {
        {"best", "Лучшее", "bestvideo+bestaudio/best", false, "mp4"},
        {"1080p", "1080p", "bestvideo[height<=1080]+bestaudio/best[height<=1080]/best", false, "mp4"},
        {"720p", "720p", "bestvideo[height<=720]+bestaudio/best[height<=720]/best", false, "mp4"},
        {"480p", "480p", "bestvideo[height<=480]+bestaudio/best[height<=480]/best", false, "mp4"},
        {"mp3", "MP3", "bestaudio/best", true, "mp3"},
    };
    return presets;
}

FormatPreset formatPreset(const QString& key) {
    for (const auto& preset : formatPresets()) {
        if (preset.key == key) return preset;
    }
    return formatPresets().first();
}

bool isVersionNewer(const QString& candidate, const QString& installed) {
    const QVersionNumber candidateVersion = QVersionNumber::fromString(candidate.trimmed());
    const QVersionNumber installedVersion = QVersionNumber::fromString(installed.trimmed());
    return !candidateVersion.isNull() &&
           (installedVersion.isNull() || QVersionNumber::compare(candidateVersion, installedVersion) > 0);
}

static QStringList jsRuntimeArguments(const QString& denoPath) {
    return denoPath.isEmpty() ? QStringList{} : QStringList{"--js-runtimes", "deno:" + QDir::toNativeSeparators(denoPath)};
}

QStringList buildMetadataArguments(const QString& url, const QString& denoPath) {
    QStringList args{"--ignore-config"};
    args << jsRuntimeArguments(denoPath)
         << "--dump-single-json" << "--skip-download" << "--no-playlist" << url;
    return args;
}

QStringList buildDownloadArguments(const QString& url, const FormatPreset& preset,
                                   const QString& outputDir, const QString& ffmpegDir,
                                   const QString& denoPath) {
    QStringList args{"--newline", "--ignore-config", "--no-playlist", "--progress"};
    args << jsRuntimeArguments(denoPath)
         << "--progress-template"
         << "download:download:%(progress._percent_str)s|%(progress._speed_str)s|%(progress._eta_str)s|%(progress.downloaded_bytes)s|%(progress.total_bytes)s"
         << "--progress-template" << "postprocess:postprocess:%(progress.status)s"
         << "--print" << "after_move:vdppath:%(filepath)s";
    if (!ffmpegDir.isEmpty()) args << "--ffmpeg-location" << ffmpegDir;
    args << "-f" << preset.selector;
    if (preset.originalAudio) args << "-x" << "--audio-format" << "best";
    else if (preset.extractAudio) args << "-x" << "--audio-format" << preset.extension << "--audio-quality" << preset.audioQuality;
    else if (!preset.originalAudio && !preset.extension.isEmpty()) args << "--merge-output-format" << preset.extension;
    args << "-o" << QDir(outputDir).filePath("%(title).180B.%(ext)s") << url;
    return args;
}

QString sanitizeError(const QString& raw) {
    const QString text = raw.trimmed();
    const QString lower = text.toLower();
    if (lower.contains("no supported javascript runtime"))
        return "Deno не найден. Откройте «Инструменты» и восстановите runtime.";
    if (lower.contains("failed to start") || lower.contains("could not start"))
        return "Не удалось запустить загрузчик. Восстановите инструменты runtime.";
    if (lower.contains("cancelled") || lower.contains("canceled")) return "Загрузка отменена.";
    if (lower.contains("private") || lower.contains("sign in") || lower.contains("login") || lower.contains("authentication"))
        return "Видео приватное или недоступно для этого аккаунта.";
    if (lower.contains("age-restricted") || lower.contains("age restricted") || lower.contains("region") || lower.contains("geo"))
        return "Видео ограничено по возрасту или региону.";
    if (lower.contains("unavailable") || lower.contains("not available"))
        return "Видео недоступно. Возможно, оно удалено или ограничено платформой.";
    if (lower.contains("429") || lower.contains("too many requests"))
        return "Платформа временно ограничила запросы. Попробуйте позже.";
    if (lower.contains("403") || lower.contains("forbidden"))
        return "Платформа отклонила медиапоток (403). Обновите yt-dlp в разделе «Инструменты», затем проверьте сеть и настройки прокси.";
    if (lower.contains("timeout") || lower.contains("connection") || lower.contains("network"))
        return "Сетевая ошибка. Проверьте подключение и повторите попытку.";
    return text.isEmpty() ? QStringLiteral("Неизвестная ошибка.") : redactSecrets(text).right(1800);
}

AppPaths AppPaths::defaults() {
    QString base = qEnvironmentVariable("VDP_DATA_ROOT");
    if (base.isEmpty()) {
#ifdef Q_OS_WIN
    QString root = qEnvironmentVariable("LOCALAPPDATA");
    if (root.isEmpty()) root = QDir::home().filePath("AppData/Local");
    base = QDir(root).filePath(kAppName);
#elif defined(Q_OS_MACOS)
    base = QDir::home().filePath(QString("Library/Application Support/%1").arg(kAppName));
#else
    QString root = qEnvironmentVariable("XDG_DATA_HOME");
    if (root.isEmpty()) root = QDir::home().filePath(".local/share");
    base = QDir(root).filePath(kAppName);
#endif
    }
    const QString runtime = QDir(base).filePath("runtime");
    const QString data = QDir(base).filePath("data");
    return {base, runtime, QDir(runtime).filePath("yt-dlp"), QDir(runtime).filePath("deno"),
            QDir(runtime).filePath("ffmpeg/bin"), QDir(base).filePath("data"),
            QDir(base).filePath("logs"), QDir(base).filePath("cache"),
            QDir(data).filePath("settings.ini"), QDir(data).filePath("history.json"),
            QDir(runtime).filePath("manifest.json")};
}

void AppPaths::ensure() const {
    for (const auto& path : {baseDir, runtimeDir, ytdlpDir, denoDir, ffmpegBinDir, dataDir, logsDir, cacheDir})
        QDir().mkpath(path);
}

bool ToolchainStatus::ready() const {
    return ytdlp.exists && !ytdlp.version.isEmpty() && deno.exists && !deno.version.isEmpty() &&
           ffmpeg.exists && !ffmpeg.version.isEmpty() && ffprobe.exists && !ffprobe.version.isEmpty();
}

ToolchainManager::ToolchainManager(AppPaths paths, QString bundleDirectory)
    : paths_(std::move(paths)), bundleDirectory_(std::move(bundleDirectory)) { paths_.ensure(); }

QString ToolchainManager::executableName(const QString& stem) const {
#ifdef Q_OS_WIN
    return stem + ".exe";
#else
    return stem;
#endif
}

QString ToolchainManager::bundledTool(const QString& name) const {
    if (!bundleDirectory_.isEmpty()) {
        const auto path = QDir(bundleDirectory_).filePath(name);
        return QFileInfo::exists(path) ? path : QString{};
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("toolchain/" + name),
#ifdef Q_OS_MACOS
        QDir(appDir).filePath("../Resources/toolchain/" + name),
#endif
        QDir::current().filePath("build_assets/toolchain/" + name),
    };
    for (const auto& path : candidates) if (QFileInfo::exists(path)) return QDir::cleanPath(path);
    return {};
}

bool ToolchainManager::copyBundledOrSystem(const QString& stem, const QString& target) const {
    if (QFileInfo::exists(target)) return true;
    QString source = bundledTool(executableName(stem));
    if (source.isEmpty()) source = QStandardPaths::findExecutable(executableName(stem));
    if (source.isEmpty()) return false;
    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile::remove(target);
    if (!QFile::copy(source, target)) return false;
    QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::ExeOwner | QFileDevice::ExeUser |
                                      QFileDevice::ExeGroup | QFileDevice::ExeOther);
    return true;
}

QString ToolchainManager::ytdlpPath() const { return QDir(paths_.ytdlpDir).filePath(executableName("yt-dlp")); }
QString ToolchainManager::denoPath() const { return QDir(paths_.denoDir).filePath(executableName("deno")); }
QString ToolchainManager::ffmpegPath() const { return QDir(paths_.ffmpegBinDir).filePath(executableName("ffmpeg")); }
QString ToolchainManager::ffprobePath() const { return QDir(paths_.ffmpegBinDir).filePath(executableName("ffprobe")); }
QString ToolchainManager::ffmpegDirectory() const { return QFileInfo(ffmpegPath()).absolutePath(); }

QString ToolchainManager::toolVersion(const QString& path, const QStringList& args) const {
    if (!QFileInfo::exists(path)) return {};
    QProcess process;
    process.start(path, args);
    if (!process.waitForFinished(10000)) { process.kill(); process.waitForFinished(2000); return {}; }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return {};
    return QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError()).trimmed().section('\n', 0, 0).left(500);
}

static QString manifestKey(const QString& name) {
    if (name.compare("yt-dlp", Qt::CaseInsensitive) == 0) return "yt_dlp";
    return name.toLower();
}

static QJsonObject readRuntimeManifest(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

ToolchainStatus ToolchainManager::ensureRuntime() {
    paths_.ensure();
    const auto current = status(true);
    struct BundleFile { QString stem, source, target; QStringList args; ToolInfo installed; };
    const QVector<BundleFile> candidates = {
        {"yt-dlp", bundledTool(executableName("yt-dlp")), ytdlpPath(), {"--version"}, current.ytdlp},
        {"deno", bundledTool(executableName("deno")), denoPath(), {"--version"}, current.deno},
        {"ffmpeg", bundledTool(executableName("ffmpeg")), ffmpegPath(), {"-version"}, current.ffmpeg},
        {"ffprobe", bundledTool(executableName("ffprobe")), ffprobePath(), {"-version"}, current.ffprobe}};
    auto verifiedBundle = [](const BundleFile& file) {
        if (file.source.isEmpty()) return false;
        const auto manifest = readRuntimeManifest(QDir(QFileInfo(file.source).absolutePath()).filePath("manifest.json"));
        const auto proof = manifest.value(manifestKey(file.stem)).toObject();
        const auto expected = proof.value("sha256").toString().toLatin1().toLower();
        return proof.value("verified").toBool() && expected.size() == 64 && fileSha256(file.source) == expected;
    };
    auto installGroup = [&](const QVector<BundleFile>& group) {
        QVector<InstallEntry> entries;
        QHash<QString, QStringList> arguments;
        QHash<QString, QString> versions;
        for (const auto& file : group) {
            if (!verifiedBundle(file)) return;
            entries.append({file.source, file.target});
            arguments.insert(file.source, file.args); arguments.insert(file.target, file.args);
        }
        QString error;
        auto validate = [&](const QString& path, QString* message) {
            const auto version = toolVersion(path, arguments.value(path));
            if (version.isEmpty()) { if (message) *message = "Не удалось проверить версию runtime"; return false; }
            versions.insert(path, version); return true;
        };
        auto commit = [&](QString* message) {
            QVector<ToolInfo> tools;
            for (const auto& file : group) tools.append({file.stem, file.target, versions.value(file.target), true, true,
                QString::fromLatin1(fileSha256(file.target)), "bundled:" + file.source});
            return recordVerifiedTools(tools, message);
        };
        installToolFiles(entries, validate, &error, commit);
    };
    const auto numericVersion = [](const QString& version) {
        return QRegularExpression("[0-9]+(?:\\.[0-9]+)+").match(version).captured();
    };
    for (int index = 0; index < 2; ++index) {
        const auto& file = candidates.at(index);
        if (file.source.isEmpty()) {
            if (!file.installed.exists) copyBundledOrSystem(file.stem, file.target);
            continue;
        }
        if (!verifiedBundle(file)) continue;
        const bool invalid = !file.installed.exists || file.installed.version.isEmpty() ||
            (!file.installed.sha256.isEmpty() && !file.installed.verified);
        if (invalid || isVersionNewer(numericVersion(toolVersion(file.source, file.args)),
                                      numericVersion(file.installed.version))) installGroup({file});
    }
    const bool repairPair = !current.ffmpeg.exists || !current.ffprobe.exists || current.ffmpeg.version.isEmpty() ||
        current.ffprobe.version.isEmpty() || (!current.ffmpeg.sha256.isEmpty() && !current.ffmpeg.verified) ||
        (!current.ffprobe.sha256.isEmpty() && !current.ffprobe.verified);
    if (repairPair) {
        const auto& ffmpeg = candidates.at(2);
        const auto& ffprobe = candidates.at(3);
        if (verifiedBundle(ffmpeg) && verifiedBundle(ffprobe)) installGroup({ffmpeg, ffprobe});
        else if (ffmpeg.source.isEmpty() && ffprobe.source.isEmpty()) {
            // Explicitly local PATH tools remain compatible, with unknown integrity.
            if (!current.ffmpeg.exists) copyBundledOrSystem("ffmpeg", ffmpeg.target);
            if (!current.ffprobe.exists) copyBundledOrSystem("ffprobe", ffprobe.target);
        }
    }
    auto result = status(true);
    writeManifest(result);
    return result;
}
ToolchainStatus ToolchainManager::status(bool refreshVersions) const {
    const auto manifest = readRuntimeManifest(paths_.manifestFile);
    auto make = [&](const QString& name, const QString& path, const QStringList& args) {
        const QFileInfo file(path);
        const auto record = manifest.value(manifestKey(name)).toObject();
        ToolInfo info{name, path, {}, file.isFile(), false, {}, {}};
        info.version = record.value("version").toString();
        info.source = record.value("source").toString();
        info.sha256 = record.value("sha256").toString();
        const bool identityMatches = record.value("path").toString() == path &&
            record.value("size").toVariant().toLongLong() == file.size() &&
            record.value("modifiedMs").toString() == QString::number(file.lastModified().toMSecsSinceEpoch());
        const bool hasProof = info.sha256.size() == 64 && !info.source.isEmpty();
        if (info.exists && hasProof) {
            info.verified = refreshVersions ? fileSha256(path) == info.sha256.toLatin1() : identityMatches;
        }
        if (hasProof && !info.verified) info.version.clear();
        else if (refreshVersions && info.exists) info.version = toolVersion(path, args);
        if (!info.exists) { info.version.clear(); info.sha256.clear(); }
        return info;
    };
    ToolchainStatus result{
        make("yt-dlp", ytdlpPath(), {"--version"}), make("Deno", denoPath(), {"--version"}),
        make("ffmpeg", ffmpegPath(), {"-version"}), make("ffprobe", ffprobePath(), {"-version"}), {}};
    if (!result.ytdlp.exists) result.warning = "yt-dlp не найден";
    else if (!result.deno.exists) result.warning = "Deno не найден — YouTube будет работать нестабильно";
    else if (!result.ffmpeg.exists || !result.ffprobe.exists) result.warning = "ffmpeg/ffprobe не найден";
    else if (refreshVersions && (result.ytdlp.version.isEmpty() || result.deno.version.isEmpty() ||
             result.ffmpeg.version.isEmpty() || result.ffprobe.version.isEmpty()))
        result.warning = "Не удалось проверить запуск одного из инструментов. Восстановите runtime.";
    return result;
}

ToolchainStatus ToolchainManager::repairRuntime() { return ensureRuntime(); }

bool ToolchainManager::recordVerifiedTools(const QVector<ToolInfo>& tools, QString* error) const {
    auto current = status(false);
    for (const auto& info : tools) {
        if (manifestKey(info.name) == "yt_dlp") current.ytdlp = info;
        else if (manifestKey(info.name) == "deno") current.deno = info;
        else if (manifestKey(info.name) == "ffmpeg") current.ffmpeg = info;
        else if (manifestKey(info.name) == "ffprobe") current.ffprobe = info;
    }
    return writeManifest(current, error);
}

bool ToolchainManager::writeManifest(const ToolchainStatus& status, QString* error) const {
    auto jsonTool = [](const ToolInfo& info) {
        const QFileInfo file(info.path);
        return QJsonObject{{"path", info.path}, {"version", info.version}, {"exists", info.exists},
            {"verified", info.verified}, {"sha256", info.sha256}, {"source", info.source},
            {"size", file.size()}, {"modifiedMs", QString::number(file.lastModified().toMSecsSinceEpoch())}};
    };
    const QJsonObject root{{"schema", 3}, {"yt_dlp", jsonTool(status.ytdlp)}, {"deno", jsonTool(status.deno)},
                          {"ffmpeg", jsonTool(status.ffmpeg)}, {"ffprobe", jsonTool(status.ffprobe)}};
    QSaveFile file(paths_.manifestFile);
    const auto bytes = QJsonDocument(root).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace vdp
