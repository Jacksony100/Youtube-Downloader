#include "tool_updater.hpp"
#include "checksum_parser.hpp"
#include "tool_install.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>

namespace vdp {
namespace {
QString probeVersion(const QString& path, const QStringList& args, int timeout, QString* error) {
    QProcess process;
    process.start(path, args);
    if (!process.waitForFinished(timeout)) {
        process.kill();
        process.waitForFinished(2000);
        if (error) *error = "Превышено время проверки версии runtime";
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) *error = "Runtime не прошёл проверку запуска";
        return {};
    }
    const auto output = QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError()).trimmed();
    if (output.isEmpty()) { if (error) *error = "Runtime не сообщил версию"; return {}; }
    return output.section('\n', 0, 0).left(500);
}

bool extractArchive(const QString& archive, const QString& directory, QString* error) {
    if (!QDir().mkpath(directory)) { *error = "Не удалось создать временный каталог"; return false; }
    QProcess process;
#ifdef Q_OS_WIN
    // .NET rejects traversal paths during extraction; the destination is always a fresh staging directory.
    process.start("powershell.exe", {"-NoProfile", "-NonInteractive", "-Command",
        QString("$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.IO.Compression.FileSystem; "
                "[System.IO.Compression.ZipFile]::ExtractToDirectory('%1','%2')")
            .arg(QString(archive).replace("'", "''"), QString(directory).replace("'", "''"))});
#elif defined(Q_OS_MACOS)
    process.start("/usr/bin/ditto", {"-x", "-k", archive, directory});
#else
    process.start("unzip", {"-o", archive, "-d", directory});
#endif
    if (!process.waitForFinished(120000)) {
        process.kill(); process.waitForFinished(2000);
        *error = "Превышено время распаковки runtime"; return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = "Не удалось распаковать проверенный архив runtime"; return false;
    }
    return true;
}

QString uniqueBinary(const QString& directory, const QString& name) {
    QDirIterator files(directory, {name}, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    QString result;
    const QString prefix = QFileInfo(directory).canonicalFilePath() + '/';
    while (files.hasNext()) {
        const QString candidate = files.next();
        if (!result.isEmpty() || !QFileInfo(candidate).canonicalFilePath().startsWith(prefix)) return {};
        result = candidate;
    }
    return result;
}

QString targetPath(const ToolchainManager& manager, const QString& target) {
    if (target == "yt-dlp") return manager.ytdlpPath();
    if (target == "deno") return manager.denoPath();
    if (target == "ffmpeg") return manager.ffmpegPath();
    if (target == "ffprobe") return manager.ffprobePath();
    return {};
}
}

ToolUpdater::ToolUpdater(AppPaths paths, QObject* parent, QNetworkAccessManager* network, ToolUpdaterOptions options)
    : QObject(parent), paths_(std::move(paths)), network_(network ? network : new QNetworkAccessManager(this)),
      options_(std::move(options)) {
    timeout_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] { fail("Превышено время загрузки runtime"); });
}
ToolUpdater::~ToolUpdater() {
    if (reply_) { reply_->disconnect(this); reply_->abort(); reply_->deleteLater(); }
}

bool ToolUpdater::start(const QString& toolId) {
    if (busy_) return false;
    QString error;
    const auto spec = loadToolchainSpec(toolId == "ytdlp" ? "yt-dlp" : toolId, &error);
    if (!spec) { emit failed(toolId, error); return false; }
    return start(*spec);
}

bool ToolUpdater::start(const ToolUpdateSpec& spec) {
    if (busy_) return false;
    QString error;
    if (!validateToolUpdateSpec(spec, &error)) { emit failed(spec.toolId, error); return false; }
    paths_.ensure();
    lock_ = std::make_unique<QLockFile>(QDir(paths_.runtimeDir).filePath(".update.lock"));
    lock_->setStaleLockTime(0);
    if (!lock_->tryLock()) {
        lock_.reset(); emit failed(spec.toolId, "Обновление runtime уже выполняется"); return false;
    }
    temporary_ = std::make_shared<QTemporaryDir>(QDir(paths_.cacheDir).filePath("runtime-XXXXXX"));
    if (!temporary_->isValid()) {
        lock_.reset(); temporary_.reset(); emit failed(spec.toolId, "Не удалось создать временный каталог runtime");
        return false;
    }
    spec_ = spec;
    artifactIndex_ = 0;
    staged_.clear();
    busy_ = true;
    installing_ = false;
    downloadArtifact();
    return true;
}

void ToolUpdater::downloadArtifact() {
    const auto& artifact = spec_.artifacts.at(artifactIndex_);
    const QString path = QDir(temporary_->path()).filePath(QString::number(artifactIndex_) + "-" + artifact.fileName);
    file_ = std::make_unique<QFile>(path);
    if (!file_->open(QIODevice::WriteOnly)) { fail("Не удалось сохранить архив runtime"); return; }
    staged_.append(path);
    hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    emit phaseChanged(spec_.toolId, "Загрузка " + artifact.fileName, true);
    request(artifact.url, false);
}

void ToolUpdater::request(const QUrl& url, bool checksum) {
    if (url.scheme() != "https") { fail("Небезопасный URL runtime"); return; }
    checksumRequest_ = checksum;
    received_ = 0;
    checksumText_.clear();
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString("VideoDownloaderPro/%1").arg(VDP_VERSION));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    request.setTransferTimeout(options_.requestTimeoutMs);
    reply_ = network_->get(request);
    reply_->setReadBufferSize(256 * 1024);
    auto* reply = reply_.data();
    connect(reply, &QNetworkReply::readyRead, this, &ToolUpdater::consume);
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (!checksumRequest_) emit progress(spec_.toolId, received, total);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { requestFinished(reply); });
    timeout_.start(qMax(1, options_.requestTimeoutMs));
}

void ToolUpdater::consume() {
    if (!reply_) return;
    while (reply_->bytesAvailable() > 0) {
        const auto chunk = reply_->read(64 * 1024);
        if (chunk.isEmpty()) break;
        received_ += chunk.size();
        if (checksumRequest_) {
            if (received_ > 1024 * 1024) { fail("Документ SHA256 слишком большой"); return; }
            checksumText_.append(chunk);
        } else {
            if (received_ > 2LL * 1024 * 1024 * 1024) { fail("Архив runtime слишком большой"); return; }
            if (!file_ || file_->write(chunk) != chunk.size()) { fail("Не удалось записать архив runtime"); return; }
            hash_->addData(chunk);
        }
    }
}

void ToolUpdater::requestFinished(QNetworkReply* reply) {
    if (reply_ != reply) return;
    consume();
    if (reply_ != reply) return;
    timeout_.stop();
    const auto networkError = reply->error();
    const bool secure = reply->url().scheme() == "https";
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply_.clear();
    reply->deleteLater();
    if (networkError != QNetworkReply::NoError || !secure || status >= 400) {
        fail("Не удалось загрузить runtime по HTTPS. Проверьте соединение и повторите попытку."); return;
    }
    if (received_ <= 0) { fail("Получен пустой файл runtime"); return; }
    if (checksumRequest_) { verifyArtifact(); return; }
    if (!file_->flush()) { fail("Не удалось сохранить архив runtime"); return; }
    file_->close();
    file_.reset();
    artifactDigest_ = hash_->result().toHex();
    hash_.reset();
    const auto& artifact = spec_.artifacts.at(artifactIndex_);
    if (artifact.sha256.isEmpty()) {
        emit phaseChanged(spec_.toolId, "Проверка SHA256", true);
        request(artifact.checksumUrl, true);
    } else verifyArtifact();
}

void ToolUpdater::verifyArtifact() {
    const auto& artifact = spec_.artifacts.at(artifactIndex_);
    auto expected = artifact.sha256;
    if (expected.isEmpty()) {
        const auto digest = findSha256(QString::fromUtf8(checksumText_), artifact.fileName, artifact.singleArtifactChecksum);
        if (!digest) { fail("SHA256 для нужного файла не найден"); return; }
        expected = *digest;
    }
    if (artifactDigest_ != expected.toLower()) { fail("SHA256 runtime не совпал. Установка отменена."); return; }
    ++artifactIndex_;
    if (artifactIndex_ < spec_.artifacts.size()) downloadArtifact();
    else install();
}

void ToolUpdater::install() {
    installing_ = true;
    emit phaseChanged(spec_.toolId, "Проверка и установка runtime", false);
    struct Result { bool ok = false; QString error; };
    auto result = std::make_shared<Result>();
    // The worker owns staging and the transaction lock even if the UI service is destroyed.
    auto lock = std::shared_ptr<QLockFile>(lock_.release());
    auto* thread = QThread::create([paths = paths_, spec = spec_, staged = staged_, temporary = temporary_,
                                    options = options_, result, lock] {
        const auto unlock = qScopeGuard([&] { lock->unlock(); });
        ToolchainManager manager(paths);
        QVector<InstallEntry> entries;
        QHash<QString, ToolBinary> descriptions;
        QHash<QString, QString> sources;
        for (qsizetype index = 0; index < spec.artifacts.size(); ++index) {
            const auto& artifact = spec.artifacts.at(index);
            const QString extracted = QDir(temporary->path()).filePath("extract-" + QString::number(index));
            if (artifact.archive && !extractArchive(staged.at(index), extracted, &result->error)) return;
            for (const auto& binary : artifact.binaries) {
                const QString source = artifact.archive ? uniqueBinary(extracted, binary.name) : staged.at(index);
                const QString target = targetPath(manager, binary.target);
                if (source.isEmpty() || target.isEmpty()) { result->error = "Нужный исполняемый файл не найден в архиве"; return; }
                QFile::setPermissions(source, QFile::permissions(source) | QFileDevice::ExeOwner |
                    QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther);
                entries.append({source, target});
                descriptions.insert(source, binary); descriptions.insert(target, binary);
                sources.insert(target, artifact.url.toString());
            }
        }
        QHash<QString, QString> versions;
        auto validate = [&](const QString& path, QString* error) {
            const auto args = descriptions.value(path).versionArgs;
            const auto version = options.versionProbe ? options.versionProbe(path, args, error) :
                                 probeVersion(path, args, options.versionTimeoutMs, error);
            if (version.isEmpty()) return false;
            versions.insert(path, version);
            return true;
        };
        auto commit = [&](QString* error) {
            QVector<ToolInfo> tools;
            for (const auto& entry : entries) {
                const auto digest = fileSha256(entry.target);
                if (digest.isEmpty()) { *error = "Не удалось проверить установленный файл"; return false; }
                tools.append({descriptions.value(entry.target).target, entry.target, versions.value(entry.target),
                              true, true, QString::fromLatin1(digest), sources.value(entry.target)});
            }
            return manager.recordVerifiedTools(tools, error);
        };
        result->ok = installToolFiles(entries, validate, &result->error, commit);
    });
    connect(thread, &QThread::finished, this, [this, result] {
        const auto id = spec_.toolId;
        cleanup();
        if (result->ok) emit finished(id);
        else emit failed(id, result->error);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void ToolUpdater::cancel() {
    if (!canCancel()) return;
    const auto id = spec_.toolId;
    cleanup();
    emit cancelled(id);
}
void ToolUpdater::fail(const QString& message) {
    if (!busy_) return;
    const auto id = spec_.toolId;
    cleanup();
    emit failed(id, message);
}
void ToolUpdater::cleanup() {
    timeout_.stop();
    if (reply_) { auto* reply = reply_.data(); reply_.clear(); reply->disconnect(this); reply->abort(); reply->deleteLater(); }
    file_.reset(); hash_.reset(); temporary_.reset(); lock_.reset();
    staged_.clear(); checksumText_.clear(); busy_ = false; installing_ = false;
}
}
