#pragma once

#include "core.hpp"
#include "toolchain_lock.hpp"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <memory>

class QFile;
class QCryptographicHash;
class QLockFile;
class QNetworkAccessManager;
class QNetworkReply;
class QTemporaryDir;

namespace vdp {
using ToolVersionProbe = std::function<QString(const QString&, const QStringList&, QString*)>;
struct ToolUpdaterOptions {
    int requestTimeoutMs = 240000;
    int versionTimeoutMs = 10000;
    ToolVersionProbe versionProbe;
};

class ToolUpdater final : public QObject {
    Q_OBJECT
public:
    explicit ToolUpdater(AppPaths paths, QObject* parent = nullptr,
                         QNetworkAccessManager* network = nullptr, ToolUpdaterOptions options = {});
    ~ToolUpdater() override;
    bool start(const QString& toolId);
    bool start(const ToolUpdateSpec& spec);
    bool isBusy() const { return busy_; }
    bool canCancel() const { return busy_ && !installing_; }
public slots:
    void cancel();
signals:
    void progress(const QString& toolId, qint64 received, qint64 total);
    void phaseChanged(const QString& toolId, const QString& message, bool cancellable);
    void finished(const QString& toolId);
    void failed(const QString& toolId, const QString& message);
    void cancelled(const QString& toolId);
private:
    void downloadArtifact();
    void request(const QUrl& url, bool checksum);
    void consume();
    void requestFinished(QNetworkReply* reply);
    void verifyArtifact();
    void install();
    void fail(const QString& message);
    void cleanup();
    AppPaths paths_;
    QNetworkAccessManager* network_;
    ToolUpdaterOptions options_;
    ToolUpdateSpec spec_;
    QPointer<QNetworkReply> reply_;
    std::unique_ptr<QFile> file_;
    std::unique_ptr<QCryptographicHash> hash_;
    std::unique_ptr<QLockFile> lock_;
    std::shared_ptr<QTemporaryDir> temporary_;
    QTimer timeout_;
    QStringList staged_;
    QByteArray checksumText_;
    QByteArray artifactDigest_;
    int artifactIndex_ = 0;
    qint64 received_ = 0;
    bool checksumRequest_ = false;
    bool busy_ = false;
    bool installing_ = false;
};
}
