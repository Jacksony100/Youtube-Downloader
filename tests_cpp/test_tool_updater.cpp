#include "runtime/tool_updater.hpp"
#include "runtime/checksum_parser.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <atomic>
#include <cstring>
using namespace vdp;

struct Response {
    QByteArray data;
    int chunkSize = 4096;
    int delayMs = 2;
    bool stall = false;
    bool failure = false;
    bool downgrade = false;
};

class ControlledReply final : public QNetworkReply {
public:
    ControlledReply(const QNetworkRequest& request, const Response& response, QObject* parent)
        : QNetworkReply(parent), response_(response) {
        setRequest(request); setUrl(response.downgrade ? QUrl("http://fixture.invalid/tool") : request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, [this] {
            if (isFinished()) return;
            delivered_ = qMin<qint64>(response_.data.size(), delivered_ + response_.chunkSize);
            emit readyRead();
            if (isFinished()) return;
            emit downloadProgress(delivered_, response_.data.size());
            if (delivered_ == response_.data.size()) {
                timer_->stop();
                if (response_.failure) setError(RemoteHostClosedError, "fixture disconnected");
                setFinished(true); emit finished();
            }
        });
        if (!response_.stall) timer_->start(response_.delayMs);
    }
    void abort() override {
        if (isFinished()) return;
        timer_->stop(); setError(OperationCanceledError, "cancelled"); setFinished(true); emit finished();
    }
    qint64 bytesAvailable() const override { return delivered_ - consumed_ + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char* destination, qint64 maximum) override {
        const auto count = qMin(maximum, delivered_ - consumed_);
        if (count <= 0) return isFinished() ? -1 : 0;
        std::memcpy(destination, response_.data.constData() + consumed_, static_cast<size_t>(count));
        consumed_ += count; return count;
    }
private:
    Response response_;
    QTimer* timer_;
    qint64 delivered_ = 0, consumed_ = 0;
};

class ControlledNetwork final : public QNetworkAccessManager {
public:
    QHash<QString, Response> responses;
    int requests = 0;
protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        ++requests;
        return new ControlledReply(request, responses.value(request.url().path()), this);
    }
};

class ToolUpdaterTests final : public QObject {
    Q_OBJECT
    static AppPaths paths(const QTemporaryDir& temp) {
        const QString root = temp.path(), runtime = temp.filePath("runtime"), data = temp.filePath("data");
        return {root, runtime, runtime + "/yt-dlp", runtime + "/deno", runtime + "/ffmpeg/bin", data,
                root + "/logs", root + "/cache", data + "/settings.ini", data + "/history.json", runtime + "/manifest.json"};
    }
    static ToolUpdateSpec spec(const QByteArray& bytes, bool published = false) {
        ToolArtifact artifact;
        artifact.url = QUrl("https://fixture.invalid/tool");
        artifact.fileName = "yt-dlp.exe";
        artifact.checksumUrl = QUrl("https://fixture.invalid/sums");
        if (!published) artifact.sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
        artifact.binaries = {{"yt-dlp.exe", "yt-dlp", {"--version"}}};
        return {"yt-dlp", {artifact}};
    }
    static ToolUpdaterOptions options() {
        ToolUpdaterOptions result;
        result.requestTimeoutMs = 3000;
        result.versionProbe = [](const QString&, const QStringList&, QString*) { return "fixture 1.0"; };
        return result;
    }
    static void write(const QString& path, const QByteArray& value) {
        QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(value), value.size());
    }
    static QByteArray read(const QString& path) {
        QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }
private slots:
    void streamedTransferKeepsEventLoopResponsive() {
        QTemporaryDir temp; ControlledNetwork network;
        const QByteArray data(512 * 1024, 'x');
        network.responses.insert("/tool", {data, 16 * 1024, 3});
        const auto digest = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
        network.responses.insert("/sums", {QByteArray(64, 'a') + "  wrong.exe\n" + digest + " *yt-dlp.exe\n"});
        std::atomic_bool probeOnWorker = false;
        auto config = options();
        config.versionProbe = [&](const QString&, const QStringList&, QString*) {
            probeOnWorker = QThread::currentThread() != QCoreApplication::instance()->thread(); return "fixture 1.0";
        };
        ToolUpdater updater(paths(temp), nullptr, &network, config);
        QSignalSpy complete(&updater, &ToolUpdater::finished), failure(&updater, &ToolUpdater::failed), progress(&updater, &ToolUpdater::progress);
        int heartbeats = 0;
        QTimer heartbeat; connect(&heartbeat, &QTimer::timeout, this, [&] { ++heartbeats; }); heartbeat.start(1);
        QVERIFY(updater.start(spec(data, true)));
        QVERIFY(!updater.start(spec(data)));
        QTRY_VERIFY_WITH_TIMEOUT(complete.count() + failure.count() == 1, 5000);
        if (!failure.isEmpty())
            QFAIL(qPrintable(failure.at(0).at(1).toString()));
        QCOMPARE(complete.count(), 1); QVERIFY(heartbeats > 10); QVERIFY(progress.count() > 10); QVERIFY(probeOnWorker.load());
        ToolchainManager manager(paths(temp));
        QCOMPARE(read(manager.ytdlpPath()), data);
        QVERIFY(manager.status().ytdlp.verified);
        QCOMPARE(manager.status().ytdlp.sha256.toLatin1(), digest);
        QCOMPARE(network.requests, 2);
    }
    void wrongChecksumLeavesOriginalAndManifest() {
        QTemporaryDir temp; ControlledNetwork network;
        network.responses.insert("/tool", {"bad"});
        ToolchainManager manager(paths(temp)); write(manager.ytdlpPath(), "old"); write(paths(temp).manifestFile, "old manifest");
        ToolUpdater updater(paths(temp), nullptr, &network, options());
        QSignalSpy failure(&updater, &ToolUpdater::failed);
        QVERIFY(updater.start(spec("expected")));
        QTRY_COMPARE(failure.count(), 1);
        QCOMPARE(read(manager.ytdlpPath()), QByteArray("old"));
        QCOMPARE(read(paths(temp).manifestFile), QByteArray("old manifest"));
        QVERIFY(!updater.isBusy());
        QCOMPARE(QDir(paths(temp).cacheDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
    }
    void cancelRemovesStaging() {
        QTemporaryDir temp; ControlledNetwork network;
        network.responses.insert("/tool", {QByteArray(1024 * 1024, 'x'), 1024, 10});
        ToolUpdater updater(paths(temp), nullptr, &network, options());
        QSignalSpy cancelled(&updater, &ToolUpdater::cancelled), failed(&updater, &ToolUpdater::failed), progress(&updater, &ToolUpdater::progress);
        QVERIFY(updater.start(spec("content")));
        QTRY_VERIFY(progress.count() > 0);
        updater.cancel(); updater.cancel();
        QCOMPARE(cancelled.count(), 1); QCOMPARE(failed.count(), 0); QVERIFY(!updater.isBusy());
        QCOMPARE(QDir(paths(temp).cacheDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
        QVERIFY(!QFileInfo::exists(ToolchainManager(paths(temp)).ytdlpPath()));
    }
    void timeoutAborts() {
        QTemporaryDir temp; ControlledNetwork network;
        network.responses.insert("/tool", {"", 1, 1, true});
        auto config = options(); config.requestTimeoutMs = 30;
        ToolUpdater updater(paths(temp), nullptr, &network, config);
        QSignalSpy failure(&updater, &ToolUpdater::failed);
        QVERIFY(updater.start(spec("content"))); QTRY_COMPARE(failure.count(), 1);
        QVERIFY(!updater.isBusy());
        QCOMPARE(QDir(paths(temp).cacheDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 0);
    }
    void httpAndDowngradeRejected() {
        QTemporaryDir temp; ControlledNetwork network;
        ToolUpdater updater(paths(temp), nullptr, &network, options());
        auto description = spec("data"); description.artifacts[0].url = QUrl("http://fixture.invalid/tool");
        QVERIFY(!updater.start(description)); QCOMPARE(network.requests, 0);
        network.responses.insert("/tool", {"data", 4096, 1, false, false, true});
        QSignalSpy failure(&updater, &ToolUpdater::failed);
        QVERIFY(updater.start(spec("data"))); QTRY_COMPARE(failure.count(), 1);
        QVERIFY(!QFileInfo::exists(ToolchainManager(paths(temp)).ytdlpPath()));
    }
    void failedVersionRestoresPreviousRuntime() {
        QTemporaryDir temp; ControlledNetwork network;
        ToolchainManager manager(paths(temp)); write(manager.ytdlpPath(), "old");
        network.responses.insert("/tool", {"new"});
        auto config = options();
        config.versionProbe = [target = manager.ytdlpPath()](const QString& path, const QStringList&, QString*) {
            return path == target ? QString{} : QString("fixture 1.0");
        };
        ToolUpdater updater(paths(temp), nullptr, &network, config);
        QSignalSpy failure(&updater, &ToolUpdater::failed);
        QVERIFY(updater.start(spec("new"))); QTRY_COMPARE(failure.count(), 1);
        QCOMPARE(read(manager.ytdlpPath()), QByteArray("old"));
        QVERIFY(!QFileInfo::exists(paths(temp).manifestFile));
    }
    void sharedRuntimeUpdateLock() {
        QTemporaryDir temp; ControlledNetwork network;
        network.responses.insert("/tool", {"", 1, 1, true});
        ToolUpdater first(paths(temp), nullptr, &network, options()), second(paths(temp), nullptr, &network, options());
        QVERIFY(first.start(spec("data"))); QVERIFY(!second.start(spec("data")));
        first.cancel(); QVERIFY(second.start(spec("data"))); second.cancel();
    }
};
QTEST_GUILESS_MAIN(ToolUpdaterTests)
#include "test_tool_updater.moc"
