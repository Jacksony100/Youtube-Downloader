#include "core.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QUrl>

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
    QStringList args{"--newline", "--ignore-config", "--no-playlist"};
    args << jsRuntimeArguments(denoPath)
         << "--progress-template"
         << "download:%(progress._percent_str)s|%(progress._speed_str)s|%(progress._eta_str)s|%(progress.downloaded_bytes)s|%(progress.total_bytes)s"
         << "--print" << "after_move:vdppath:%(filepath)s";
    if (!ffmpegDir.isEmpty()) args << "--ffmpeg-location" << ffmpegDir;
    args << "-f" << preset.selector;
    if (preset.extractAudio) args << "-x" << "--audio-format" << "mp3" << "--audio-quality" << "192K";
    else args << "--merge-output-format" << "mp4";
    args << "-o" << QDir(outputDir).filePath("%(title).180B.%(ext)s") << url;
    return args;
}

QString sanitizeError(const QString& raw) {
    const QString text = raw.trimmed();
    const QString lower = text.toLower();
    if (lower.contains("no supported javascript runtime"))
        return "Deno не найден. Откройте «Инструменты» и восстановите runtime.";
    if (lower.contains("private")) return "Видео приватное или недоступно для этого аккаунта.";
    if (lower.contains("unavailable") || lower.contains("not available"))
        return "Видео недоступно. Возможно, оно удалено или ограничено платформой.";
    if (lower.contains("age") || lower.contains("region") || lower.contains("geo"))
        return "Видео ограничено по возрасту или региону.";
    if (lower.contains("429") || lower.contains("too many requests"))
        return "Платформа временно ограничила запросы. Попробуйте позже.";
    if (lower.contains("timeout") || lower.contains("connection") || lower.contains("network"))
        return "Сетевая ошибка. Проверьте подключение и повторите попытку.";
    return text.isEmpty() ? QStringLiteral("Неизвестная ошибка.") : text.right(1800);
}

AppPaths AppPaths::defaults() {
#ifdef Q_OS_WIN
    QString root = qEnvironmentVariable("LOCALAPPDATA");
    if (root.isEmpty()) root = QDir::home().filePath("AppData/Local");
    const QString base = QDir(root).filePath(kAppName);
#elif defined(Q_OS_MACOS)
    const QString base = QDir::home().filePath(QString("Library/Application Support/%1").arg(kAppName));
#else
    QString root = qEnvironmentVariable("XDG_DATA_HOME");
    if (root.isEmpty()) root = QDir::home().filePath(".local/share");
    const QString base = QDir(root).filePath(kAppName);
#endif
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
    return ytdlp.exists && deno.exists && ffmpeg.exists && ffprobe.exists;
}

ToolchainManager::ToolchainManager(AppPaths paths) : paths_(std::move(paths)) { paths_.ensure(); }

QString ToolchainManager::executableName(const QString& stem) const {
#ifdef Q_OS_WIN
    return stem + ".exe";
#else
    return stem;
#endif
}

QString ToolchainManager::bundledTool(const QString& name) const {
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
    if (!process.waitForFinished(10000)) { process.kill(); return {}; }
    return QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError()).trimmed().section('\n', 0, 0);
}

ToolchainStatus ToolchainManager::ensureRuntime() {
    paths_.ensure();
    copyBundledOrSystem("yt-dlp", ytdlpPath());
    copyBundledOrSystem("deno", denoPath());
    copyBundledOrSystem("ffmpeg", ffmpegPath());
    copyBundledOrSystem("ffprobe", ffprobePath());
    auto result = status(true);
    writeManifest(result);
    return result;
}

ToolchainStatus ToolchainManager::status(bool refreshVersions) const {
    auto make = [&](const QString& name, const QString& path, const QStringList& args) {
        ToolInfo info{name, path, {}, QFileInfo::exists(path), true};
        if (refreshVersions && info.exists) info.version = toolVersion(path, args);
        return info;
    };
    ToolchainStatus result{
        make("yt-dlp", ytdlpPath(), {"--version"}),
        make("Deno", denoPath(), {"--version"}),
        make("ffmpeg", ffmpegPath(), {"-version"}),
        make("ffprobe", ffprobePath(), {"-version"}),
        {}};
    if (!result.ytdlp.exists) result.warning = "yt-dlp не найден";
    else if (!result.deno.exists) result.warning = "Deno не найден — YouTube будет работать нестабильно";
    else if (!result.ffmpeg.exists || !result.ffprobe.exists) result.warning = "ffmpeg/ffprobe не найден";
    return result;
}

bool ToolchainManager::downloadFile(const QString& url, const QString& target, QString* error) const {
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QString("VideoDownloaderPro/%1").arg(VDP_VERSION));
    auto* reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError) {
        if (error) *error = reply->errorString();
        reply->deleteLater();
        return false;
    }
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(reply->readAll()) <= 0 || !file.commit()) {
        if (error) *error = "Не удалось сохранить " + target;
        reply->deleteLater();
        return false;
    }
    reply->deleteLater();
    return true;
}

QString ToolchainManager::downloadText(const QString& url, QString* error) const {
    QTemporaryDir temp;
    const QString file = QDir(temp.path()).filePath("response.txt");
    if (!downloadFile(url, file, error)) return {};
    QFile input(file);
    return input.open(QIODevice::ReadOnly) ? QString::fromUtf8(input.readAll()) : QString{};
}

bool ToolchainManager::verifyPublishedSha256(const QString& file, const QString& checksumUrl, QString* error) const {
    const QString text = downloadText(checksumUrl, error);
    if (text.isEmpty()) return false;
    QRegularExpression regex("(?i)(?:^|\\s)([0-9a-f]{64})(?:\\s|$)");
    const auto match = regex.match(text);
    if (!match.hasMatch()) { if (error) *error = "SHA256 не найден в ответе"; return false; }
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&input);
    const bool ok = QString::fromLatin1(hash.result().toHex()).compare(match.captured(1), Qt::CaseInsensitive) == 0;
    if (!ok && error) *error = "SHA256 не совпал";
    return ok;
}

bool ToolchainManager::installFile(const QString& source, const QString& target, QString* error) const {
    QDir().mkpath(QFileInfo(target).absolutePath());
    const QString backup = target + ".bak";
    QFile::remove(backup);
    if (QFileInfo::exists(target) && !QFile::rename(target, backup)) { if (error) *error = "Runtime занят"; return false; }
    if (!QFile::copy(source, target)) {
        QFile::rename(backup, target);
        if (error) *error = "Не удалось установить runtime";
        return false;
    }
    QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::ExeOwner | QFileDevice::ExeUser |
                                      QFileDevice::ExeGroup | QFileDevice::ExeOther);
    QFile::remove(backup);
    return true;
}

bool ToolchainManager::extractArchive(const QString& archive, const QString& targetDir, QString* error) const {
    QDir().mkpath(targetDir);
    QProcess process;
#ifdef Q_OS_WIN
    process.start("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command",
        QString("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
            .arg(QString(archive).replace("'", "''"), QString(targetDir).replace("'", "''"))});
#elif defined(Q_OS_MACOS)
    process.start("/usr/bin/ditto", {"-x", "-k", archive, targetDir});
#else
    process.start("unzip", {"-o", archive, "-d", targetDir});
#endif
    if (!process.waitForFinished(120000) || process.exitCode() != 0) {
        if (error) *error = QString::fromUtf8(process.readAllStandardError());
        return false;
    }
    return true;
}

QString ToolchainManager::findRecursively(const QString& root, const QString& fileName) const {
    QDirIterator it(root, {fileName}, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext() ? it.next() : QString{};
}

bool ToolchainManager::updateYtdlp(QString* error) {
#ifdef Q_OS_WIN
    const QString name = "yt-dlp.exe";
#elif defined(Q_OS_MACOS)
    const QString name = "yt-dlp_macos";
#else
    const QString name = "yt-dlp";
#endif
    QTemporaryDir temp;
    const QString staged = QDir(temp.path()).filePath(name);
    const QString base = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/";
    if (!downloadFile(base + name, staged, error) ||
        !verifyPublishedSha256(staged, base + "SHA2-256SUMS", error)) return false;
    return installFile(staged, ytdlpPath(), error);
}

bool ToolchainManager::updateDeno(QString* error) {
    QString arch = QSysInfo::currentCpuArchitecture().contains("arm") ? "aarch64" : "x86_64";
#ifdef Q_OS_WIN
    const QString platform = "pc-windows-msvc";
#elif defined(Q_OS_MACOS)
    const QString platform = "apple-darwin";
#else
    const QString platform = "unknown-linux-gnu";
#endif
    const QString name = QString("deno-%1-%2.zip").arg(arch, platform);
    const QString url = "https://github.com/denoland/deno/releases/latest/download/" + name;
    QTemporaryDir temp;
    const QString archive = QDir(temp.path()).filePath(name);
    const QString extracted = QDir(temp.path()).filePath("extracted");
    if (!downloadFile(url, archive, error) || !verifyPublishedSha256(archive, url + ".sha256sum", error) ||
        !extractArchive(archive, extracted, error)) return false;
    const QString binary = findRecursively(extracted, executableName("deno"));
    if (binary.isEmpty()) { if (error) *error = "Deno не найден в архиве"; return false; }
    return installFile(binary, denoPath(), error);
}

bool ToolchainManager::updateFfmpeg(QString* error) {
#ifndef Q_OS_WIN
    if (error) *error = "Автообновление ffmpeg сейчас доступно только в Windows";
    return false;
#else
    QTemporaryDir temp;
    const QString archive = QDir(temp.path()).filePath("ffmpeg.zip");
    const QString extracted = QDir(temp.path()).filePath("extracted");
    if (!downloadFile("https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip", archive, error) ||
        !extractArchive(archive, extracted, error)) return false;
    const QString ffmpeg = findRecursively(extracted, "ffmpeg.exe");
    const QString ffprobe = findRecursively(extracted, "ffprobe.exe");
    if (ffmpeg.isEmpty() || ffprobe.isEmpty()) { if (error) *error = "ffmpeg не найден в архиве"; return false; }
    return installFile(ffmpeg, ffmpegPath(), error) && installFile(ffprobe, ffprobePath(), error);
#endif
}

ToolchainStatus ToolchainManager::repairRuntime() {
    auto result = ensureRuntime();
    QString ignored;
    if (!result.ytdlp.exists) updateYtdlp(&ignored);
    if (!result.deno.exists) updateDeno(&ignored);
    if (!result.ffmpeg.exists || !result.ffprobe.exists) updateFfmpeg(&ignored);
    result = status(true);
    writeManifest(result);
    return result;
}

void ToolchainManager::writeManifest(const ToolchainStatus& status) const {
    auto jsonTool = [](const ToolInfo& info) {
        return QJsonObject{{"path", info.path}, {"version", info.version}, {"exists", info.exists}, {"verified", info.verified}};
    };
    QJsonObject root{{"schema", 2}, {"yt_dlp", jsonTool(status.ytdlp)}, {"deno", jsonTool(status.deno)},
                     {"ffmpeg", jsonTool(status.ffmpeg)}, {"ffprobe", jsonTool(status.ffprobe)}};
    QSaveFile file(paths_.manifestFile);
    if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(root).toJson()); file.commit(); }
}

} // namespace vdp
