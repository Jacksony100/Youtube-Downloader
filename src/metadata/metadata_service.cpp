#include "metadata_service.hpp"
#include "format_normalizer.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QTimer>

namespace vdp {
namespace { constexpr qsizetype maximumMetadataBytes = 16 * 1024 * 1024; }

MetadataService::MetadataService(const ToolchainManager* tools, QObject* parent)
    : MetadataService([tools](const QString& url) {
        return tools ? MetadataCommand{tools->ytdlpPath(), buildMetadataArguments(url, tools->denoPath())}
                     : MetadataCommand{};
    }, parent) {}

MetadataService::MetadataService(CommandBuilder commands, QObject* parent)
    : QObject(parent), commands_(std::move(commands)) {}

MetadataService::~MetadataService() {
    shuttingDown_ = true;
    for (const auto& job : jobs_) {
        if (auto* process = job->process.data()) {
            process->disconnect(this);
            for (auto* timer : process->findChildren<QTimer*>()) timer->disconnect(this);
            if (process->state() != QProcess::NotRunning) {
                process->kill();
                process->waitForFinished(1500);
            }
        }
    }
}

bool MetadataService::takeSubscription(const QString& id, const QString& url, quint64 generation) {
    const auto current = subscriptions_.constFind(id);
    if (current == subscriptions_.cend() || current->generation != generation || current->url != url) return false;
    subscriptions_.remove(id);
    return true;
}

void MetadataService::request(const QString& id, const QString& url, bool refresh) {
    if (shuttingDown_) return;
    cancel(id);
    const auto generation = ++generation_;
    subscriptions_.insert(id, {url, generation});
    if (!refresh && cache_.contains(url)) {
        const auto metadata = cache_.value(url);
        QTimer::singleShot(0, this, [this, id, url, generation, metadata] {
            if (takeSubscription(id, url, generation)) emit ready(id, metadata);
        });
        return;
    }
    if (const auto existing = jobs_.value(url)) {
        existing->requests.insert(id, generation);
        return;
    }
    auto job = std::make_shared<Job>();
    job->url = url;
    job->requests.insert(id, generation);
    jobs_.insert(url, job);
    pending_.enqueue(url);
    pump();
}

void MetadataService::cancel(const QString& id) {
    subscriptions_.remove(id);
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        const auto& job = it.value();
        job->requests.remove(id);
        // Already-running shared work may still populate the cache. Unstarted orphan work is unnecessary.
        if (job->requests.isEmpty() && !job->process) {
            pending_.removeAll(job->url);
            it = jobs_.erase(it);
        } else ++it;
    }
}

void MetadataService::pump() {
    if (shuttingDown_) return;
    while (active_.size() < 3 && !pending_.isEmpty()) {
        const auto job = jobs_.value(pending_.dequeue());
        if (!job || job->requests.isEmpty()) continue;
        auto* process = new QProcess(this);
        job->process = process;
        active_.insert(job->url, job);
        connect(process, &QProcess::started, this, [job] {
            if (!job->terminalError.isEmpty()) job->process->kill();
        });
        connect(process, &QProcess::readyReadStandardOutput, this, [this, job] {
            if (job->done || !job->terminalError.isEmpty()) return;
            job->output += job->process->readAllStandardOutput();
            if (job->output.size() > maximumMetadataBytes) {
                job->output.clear();
                stop(job, QStringLiteral("Ответ метаданных слишком большой."));
            }
        });
        connect(process, &QProcess::readyReadStandardError, this, [job] {
            if (!job->done) job->errors = (job->errors + job->process->readAllStandardError()).right(16384);
        });
        connect(process, &QProcess::errorOccurred, this, [this, job](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart || error == QProcess::Crashed)
                stop(job, QStringLiteral("Не удалось запустить или завершить проверку yt-dlp."));
        });
        connect(process, &QProcess::finished, this, [this, job](int code, QProcess::ExitStatus status) {
            if (job->done) return;
            if (job->terminalError.isEmpty()) job->output += job->process->readAllStandardOutput();
            job->errors = (job->errors + job->process->readAllStandardError()).right(16384);
            finish(job, code == 0 && status == QProcess::NormalExit ? QString{}
                : sanitizeError(QString::fromUtf8(job->errors)));
        });
        auto* timer = new QTimer(process);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, job] {
            stop(job, QStringLiteral("Истекло время проверки ссылки."));
        });
        timer->start(timeoutMs_);
        const auto command = commands_(job->url);
        process->start(command.program, command.arguments);
    }
}

void MetadataService::stop(const std::shared_ptr<Job>& job, const QString& error) {
    if (job->done || shuttingDown_) return;
    if (job->terminalError.isEmpty()) job->terminalError = error;
    if (job->process->state() == QProcess::NotRunning) finish(job, error);
    else job->process->kill();
}

void MetadataService::finish(const std::shared_ptr<Job>& job, const QString& failure) {
    if (job->done || shuttingDown_) return;
    // A killed process still owns its slot until the OS confirms it has stopped.
    if (job->process->state() != QProcess::NotRunning) { stop(job, failure); return; }
    job->done = true;
    job->process->disconnect(this);
    for (auto* timer : job->process->findChildren<QTimer*>()) {
        timer->stop();
        timer->disconnect(this);
    }
    active_.remove(job->url);
    jobs_.remove(job->url);
    QString error = job->terminalError.isEmpty() ? failure : job->terminalError;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(job->output, &parseError);
    if (error.isEmpty() && (job->output.size() > maximumMetadataBytes
        || parseError.error != QJsonParseError::NoError || !document.isObject() || document.object().isEmpty()))
        error = QStringLiteral("Не удалось прочитать метаданные видео.");
    const auto subscribers = job->requests;
    const QPointer<MetadataService> self(this);
    if (error.isEmpty()) {
        const auto metadata = parseMetadata(document.object(), job->url);
        cache_.insert(job->url, metadata);
        cacheOrder_.removeAll(job->url);
        cacheOrder_.enqueue(job->url);
        while (cacheOrder_.size() > 128) cache_.remove(cacheOrder_.dequeue());
        for (auto it = subscribers.cbegin(); it != subscribers.cend(); ++it) {
            if (takeSubscription(it.key(), job->url, it.value())) emit ready(it.key(), metadata);
            if (!self) return;
        }
    } else {
        for (auto it = subscribers.cbegin(); it != subscribers.cend(); ++it) {
            if (takeSubscription(it.key(), job->url, it.value())) emit failed(it.key(), error);
            if (!self) return;
        }
    }
    job->process->deleteLater();
    QTimer::singleShot(0, this, &MetadataService::pump);
}

} // namespace vdp
