#include "core.hpp"
#include "runtime/checksum_parser.hpp"
#include "runtime/toolchain_service.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include <iostream>
using namespace vdp;

class ToolchainTests final : public QObject {
    Q_OBJECT
    static AppPaths paths(const QTemporaryDir& temp) {
        const QString root = temp.path(), runtime = temp.filePath("runtime"), data = temp.filePath("data");
        return {root, runtime, runtime + "/yt-dlp", runtime + "/deno", runtime + "/ffmpeg/bin", data,
            root + "/logs", root + "/cache", data + "/settings.ini", data + "/history.json", runtime + "/manifest.json"};
    }
    static QString executable(const QString& name) {
#ifdef Q_OS_WIN
        return name + ".exe";
#else
        return name;
#endif
    }
    static void createBundle(const QString& directory, bool verified = true) {
        QVERIFY(QDir().mkpath(directory));
        QJsonObject manifest{{"schema", 3}};
        for (const auto& name : {QString("yt-dlp"), QString("deno"), QString("ffmpeg"), QString("ffprobe")}) {
            const QString path = QDir(directory).filePath(executable(name));
            QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(), path));
            manifest.insert(name == "yt-dlp" ? "yt_dlp" : name,
                QJsonObject{{"verified", verified}, {"sha256", QString::fromLatin1(fileSha256(path))}});
        }
        QFile output(QDir(directory).filePath("manifest.json")); QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(QJsonDocument(manifest).toJson());
    }
private slots:
    void verifiedBundleProvisionAndRepairPair() {
        QTemporaryDir temp;
        const QString bundle = temp.filePath("bundle"); createBundle(bundle);
        ToolchainManager manager(paths(temp), bundle);
        auto status = manager.ensureRuntime();
        QVERIFY2(status.ready(), qPrintable(status.warning));
        QVERIFY(status.ytdlp.verified); QVERIFY(status.ffmpeg.verified); QVERIFY(status.ffprobe.verified);
        QFile corrupt(manager.ffprobePath()); QVERIFY(corrupt.open(QIODevice::Append)); corrupt.write("tampered"); corrupt.close();
        qputenv("VDP_RUNTIME_PROBE_LOG", temp.filePath("probes.log").toUtf8());
        const auto failed = manager.status(true);
        qunsetenv("VDP_RUNTIME_PROBE_LOG");
        QFile probes(temp.filePath("probes.log")); QVERIFY(probes.open(QIODevice::ReadOnly));
        QVERIFY(!probes.readAll().contains("ffprobe"));
        QVERIFY(!failed.ffprobe.verified); QVERIFY(failed.ffprobe.version.isEmpty()); QVERIFY(!failed.ready());
        status = manager.repairRuntime();
        QVERIFY2(status.ready(), qPrintable(status.warning)); QVERIFY(status.ffprobe.verified); QVERIFY(status.ffmpeg.verified);
        QCOMPARE(fileSha256(manager.ffprobePath()), fileSha256(QDir(bundle).filePath(executable("ffprobe"))));
        QFile brokenYtdlp(manager.ytdlpPath()); QVERIFY(brokenYtdlp.open(QIODevice::Append)); brokenYtdlp.write("changed"); brokenYtdlp.close();
        QVERIFY(!manager.status(true).ytdlp.verified);
        QVERIFY(manager.repairRuntime().ytdlp.verified);
    }
    void unverifiedBundleFailsClosed() {
        QTemporaryDir temp;
        const QString bundle = temp.filePath("bundle"); createBundle(bundle, false);
        ToolchainManager manager(paths(temp), bundle);
        const auto status = manager.ensureRuntime();
        QVERIFY(!status.ready()); QVERIFY(!status.ytdlp.exists); QVERIFY(!status.ffmpeg.exists); QVERIFY(!status.ffprobe.exists);
    }
    void fileExistsIsNotOperational() {
        ToolchainStatus status;
        status.ytdlp.exists = status.deno.exists = status.ffmpeg.exists = status.ffprobe.exists = true;
        QVERIFY(!status.ready());
        status.ytdlp.version = status.deno.version = status.ffmpeg.version = status.ffprobe.version = "1.0";
        QVERIFY(status.ready());
    }
    void versionRefreshKeepsEventLoopResponsive() {
        QTemporaryDir temp;
        const QString bundle = temp.filePath("bundle"); createBundle(bundle);
        ToolchainManager manager(paths(temp), bundle); QVERIFY(manager.ensureRuntime().ready());
        ToolchainService service(paths(temp)); QSignalSpy ready(&service, &ToolchainService::ready);
        int heartbeats = 0;
        QTimer timer; connect(&timer, &QTimer::timeout, this, [&] { ++heartbeats; }); timer.start(1);
        service.refresh(); QVERIFY(service.isBusy());
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        QVERIFY(heartbeats > 0); QVERIFY(!service.isBusy());
        QVERIFY(qvariant_cast<ToolchainStatus>(ready.first().first()).ready());
    }
};

int main(int argc, char** argv) {
    if (argc == 2 && (QByteArray(argv[1]) == "--version" || QByteArray(argv[1]) == "-version")) {
        const auto logPath = qEnvironmentVariable("VDP_RUNTIME_PROBE_LOG");
        if (!logPath.isEmpty()) {
            QFile log(logPath);
            if (log.open(QIODevice::Append)) log.write(QByteArray(argv[0]) + '\n');
        }
        std::cout << "runtime fixture 2.0\n"; return 0;
    }
    QCoreApplication application(argc, argv);
    ToolchainTests test; return QTest::qExec(&test, argc, argv);
}
#include "test_toolchain_service.moc"
