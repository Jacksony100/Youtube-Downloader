#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vdp {

inline constexpr auto kAppName = "VideoDownloaderPro";
inline constexpr auto kAppTitle = "Video Downloader Pro";

struct FormatPreset {
    QString key;
    QString label;
    QString selector;
    bool extractAudio = false;
    QString extension = QStringLiteral("mp4");
};

const QVector<FormatPreset>& formatPresets();
FormatPreset formatPreset(const QString& key);
QStringList buildMetadataArguments(const QString& url, const QString& denoPath);
QStringList buildDownloadArguments(const QString& url, const FormatPreset& preset,
                                   const QString& outputDir, const QString& ffmpegDir,
                                   const QString& denoPath);
QString sanitizeError(const QString& raw);

struct AppPaths {
    QString baseDir;
    QString runtimeDir;
    QString ytdlpDir;
    QString denoDir;
    QString ffmpegBinDir;
    QString dataDir;
    QString logsDir;
    QString cacheDir;
    QString settingsFile;
    QString historyFile;
    QString manifestFile;

    static AppPaths defaults();
    void ensure() const;
};

struct ToolInfo {
    QString name;
    QString path;
    QString version;
    bool exists = false;
    bool verified = true;
};

struct ToolchainStatus {
    ToolInfo ytdlp;
    ToolInfo deno;
    ToolInfo ffmpeg;
    ToolInfo ffprobe;
    QString warning;

    [[nodiscard]] bool ready() const;
};

class ToolchainManager {
public:
    explicit ToolchainManager(AppPaths paths = AppPaths::defaults());

    ToolchainStatus ensureRuntime();
    ToolchainStatus status(bool refreshVersions = false) const;
    ToolchainStatus repairRuntime();
    bool updateYtdlp(QString* error = nullptr);
    bool updateDeno(QString* error = nullptr);
    bool updateFfmpeg(QString* error = nullptr);

    [[nodiscard]] QString ytdlpPath() const;
    [[nodiscard]] QString denoPath() const;
    [[nodiscard]] QString ffmpegPath() const;
    [[nodiscard]] QString ffprobePath() const;
    [[nodiscard]] QString ffmpegDirectory() const;
    [[nodiscard]] const AppPaths& paths() const { return paths_; }

private:
    AppPaths paths_;

    QString executableName(const QString& stem) const;
    QString bundledTool(const QString& name) const;
    bool copyBundledOrSystem(const QString& stem, const QString& target) const;
    QString toolVersion(const QString& path, const QStringList& args) const;
    bool downloadFile(const QString& url, const QString& target, QString* error) const;
    QString downloadText(const QString& url, QString* error) const;
    bool verifyPublishedSha256(const QString& file, const QString& checksumUrl, QString* error) const;
    bool installFile(const QString& source, const QString& target, QString* error) const;
    bool extractArchive(const QString& archive, const QString& targetDir, QString* error) const;
    QString findRecursively(const QString& root, const QString& fileName) const;
    void writeManifest(const ToolchainStatus& status) const;
};

} // namespace vdp
