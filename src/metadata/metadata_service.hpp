#pragma once
#include "metadata_models.hpp"
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QProcess>
#include <QPointer>
#include <functional>
#include <memory>

namespace vdp {
struct MetadataCommand { QString program; QStringList arguments; };
class MetadataService final : public QObject {
    Q_OBJECT
public:
    using CommandBuilder = std::function<MetadataCommand(const QString&)>;
    explicit MetadataService(const ToolchainManager* toolchain, QObject* parent = nullptr);
    explicit MetadataService(CommandBuilder commands, QObject* parent = nullptr);
    ~MetadataService() override;
    void request(const QString& requestId, const QString& url, bool refresh = false);
    void cancel(const QString& requestId);
    int activeCount() const { return static_cast<int>(active_.size()); }
    bool isBusy() const { return !jobs_.isEmpty(); }
    void setTimeout(int milliseconds) { timeoutMs_ = qMax(1, milliseconds); }
signals:
    void ready(const QString& requestId, const vdp::VideoMetadata& metadata);
    void failed(const QString& requestId, const QString& message);
private:
    struct Job {
        QString url;
        QHash<QString, quint64> requests;
        QPointer<QProcess> process;
        QByteArray output, errors;
        QString terminalError;
        bool done = false;
    };
    struct Subscription { QString url; quint64 generation; };
    CommandBuilder commands_;
    QHash<QString, std::shared_ptr<Job>> jobs_;
    QHash<QString, Subscription> subscriptions_;
    QHash<QString, VideoMetadata> cache_;
    QQueue<QString> cacheOrder_, pending_;
    QHash<QString, std::shared_ptr<Job>> active_;
    int timeoutMs_ = 45000;
    quint64 generation_ = 0;
    bool shuttingDown_ = false;
    void pump();
    void stop(const std::shared_ptr<Job>& job, const QString& error);
    void finish(const std::shared_ptr<Job>& job, const QString& error = {});
    bool takeSubscription(const QString& id, const QString& url, quint64 generation);
};
}
