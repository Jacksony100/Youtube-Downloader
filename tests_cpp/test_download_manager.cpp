#include "downloads/download_manager.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <cstdio>

using namespace vdp;

class DownloadManagerTests final : public QObject {
    Q_OBJECT
    static DownloadRequest request(const QTemporaryDir& dir, const QString& title = "success") {
        return {"https://example.com/" + title, title, dir.path(), formatPreset("best")};
    }
    static void configure(DownloadManager& manager, int durationMs = 120) {
        manager.setCancellationTimeout(30);
        manager.setCommandBuilder([durationMs](const DownloadTaskData& task) {
            return DownloadCommand{QCoreApplication::applicationFilePath(), {"--download-child",
                task.title, QDir(task.outputDirectory).filePath(task.id + ".mp4"), QString::number(durationMs)}};
        });
    }
private slots:
    void reservesSlotsBeforeProcessStarted() {
        QTemporaryDir dir;
        DownloadManager manager(nullptr);
        configure(manager);
        manager.setParallelLimit(2);
        int created = 0;
        int maximumCreatedBeforeStarted = 0;
        manager.setProcessFactory([&](QObject* owner) {
            ++created;
            maximumCreatedBeforeStarted = qMax(maximumCreatedBeforeStarted, created - manager.runningCount());
            return new QProcess(owner);
        });
        QSignalSpy finished(&manager, &DownloadManager::taskFinished);
        for (int i = 0; i < 6; ++i) manager.enqueue(request(dir));
        QCOMPARE(manager.runningCount(), 0);
        QCOMPARE(manager.queuedCount(), 6);
        QCOMPARE(created, 0);
        int maximumRunning = 0;
        int maximumActive = 0;
        connect(&manager, &DownloadManager::queueChanged, this, [&](int running, int queued) {
            QVERIFY(running >= 0);
            QVERIFY(queued >= 0);
            maximumRunning = qMax(maximumRunning, running);
            maximumActive = qMax(maximumActive, manager.activeCount());
            QVERIFY(manager.activeCount() <= 2);
        });
        manager.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 6, 10000);
        QCOMPARE(maximumRunning, 2);
        QCOMPARE(maximumActive, 2);
        QVERIFY(maximumCreatedBeforeStarted >= 1);
        QCOMPARE(manager.runningCount(), 0);
        QCOMPARE(manager.activeCount(), 0);
        QCOMPARE(manager.queuedCount(), 0);
        for (const auto& id : manager.taskIds()) QCOMPARE(manager.task(id)->state, TaskState::Completed);
    }
    void queuedCancellationDoesNotLaunch() {
        QTemporaryDir dir;
        DownloadManager manager(nullptr);
        int created = 0;
        manager.setProcessFactory([&](QObject* parent) { ++created; return new QProcess(parent); });
        const auto id = manager.enqueue(request(dir));
        QSignalSpy finished(&manager, &DownloadManager::taskFinished);
        manager.cancel(id);
        QCOMPARE(manager.task(id)->state, TaskState::Cancelled);
        QCOMPARE(finished.count(), 1);
        manager.start();
        QTest::qWait(30);
        QCOMPARE(created, 0);
        QCOMPARE(manager.queuedCount(), 0);
        manager.cancel(id);
        QCOMPARE(finished.count(), 1);
    }
    void activeCancellationAndRetry() {
        QTemporaryDir dir;
        DownloadManager manager(nullptr);
        configure(manager, 30000);
        const auto id = manager.enqueue(request(dir));
        QSignalSpy finished(&manager, &DownloadManager::taskFinished);
        manager.start();
        QTRY_COMPARE(manager.runningCount(), 1);
        manager.cancel(id);
        QTRY_COMPARE(manager.task(id)->state, TaskState::Cancelled);
        QCOMPARE(manager.task(id)->failure.category, DownloadErrorCategory::Cancelled);
        QCOMPARE(manager.runningCount(), 0);
        QCOMPARE(manager.activeCount(), 0);
        QCOMPARE(finished.count(), 1);
        configure(manager);
        manager.retry(id);
        QCOMPARE(manager.task(id)->state, TaskState::Queued);
        QCOMPARE(manager.task(id)->outputDirectory, dir.path());
        QTRY_COMPARE_WITH_TIMEOUT(manager.task(id)->state, TaskState::Completed, 5000);
        QCOMPARE(finished.count(), 2);
        manager.retry(id);
        QCOMPARE(manager.task(id)->state, TaskState::Completed);
        QSignalSpy removed(&manager, &DownloadManager::taskRemoved);
        manager.removeTerminal(id);
        QCOMPARE(removed.count(), 1);
        QVERIFY(!manager.task(id));
        QVERIFY(manager.taskIds().isEmpty());
    }
    void failedStartReleasesCapacity() {
        QTemporaryDir dir;
        DownloadManager manager(nullptr);
        manager.setParallelLimit(1);
        manager.setCommandBuilder([&](const DownloadTaskData&) {
            return DownloadCommand{dir.filePath("does-not-exist.exe"), {}};
        });
        QSignalSpy finished(&manager, &DownloadManager::taskFinished);
        const auto first = manager.enqueue(request(dir));
        const auto second = manager.enqueue(request(dir));
        manager.start();
        QTRY_COMPARE(finished.count(), 2);
        QCOMPARE(manager.task(first)->state, TaskState::Failed);
        QCOMPARE(manager.task(second)->failure.category, DownloadErrorCategory::ProcessStartFailed);
        QCOMPARE(manager.runningCount(), 0);
        QCOMPARE(manager.activeCount(), 0);
        QCOMPARE(manager.queuedCount(), 0);
    }
    void cancellationRacingStartupStillStopsTheProcess() {
        QTemporaryDir dir;
        DownloadManager manager(nullptr);
        configure(manager, 30000);
        QString id;
        manager.setProcessFactory([&](QObject* owner) {
            auto* process = new QProcess(owner);
            connect(process, &QProcess::stateChanged, &manager, [&](QProcess::ProcessState state) {
                if (state == QProcess::Starting) manager.cancel(id);
            });
            return process;
        });
        id = manager.enqueue(request(dir));
        QSignalSpy finished(&manager, &DownloadManager::taskFinished);
        manager.start();
        QTRY_COMPARE_WITH_TIMEOUT(manager.task(id)->state, TaskState::Cancelled, 5000);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(manager.activeCount(), 0);
        QCOMPARE(manager.runningCount(), 0);
    }
    void crashAndDuplicateFinishAreFinalizedOnce() {
        QTemporaryDir dir;
        DownloadManager manager(nullptr);
        configure(manager, 30000);
        QPointer<QProcess> process;
        manager.setProcessFactory([&](QObject* owner) { process = new QProcess(owner); return process.data(); });
        QSignalSpy finished(&manager, &DownloadManager::taskFinished);
        const auto id = manager.enqueue(request(dir));
        manager.start();
        QTRY_COMPARE(manager.runningCount(), 1);
        QVERIFY(process);
        process->kill();
        QTRY_COMPARE(manager.task(id)->state, TaskState::Failed);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(manager.runningCount(), 0);
        QCOMPARE(manager.activeCount(), 0);
        // Deferred delivery after finalization must not re-finalize this attempt.
        if (process) {
            QMetaObject::invokeMethod(process, "finished", Qt::DirectConnection,
                Q_ARG(int, 0), Q_ARG(QProcess::ExitStatus, QProcess::NormalExit));
            QMetaObject::invokeMethod(process, "errorOccurred", Qt::DirectConnection,
                Q_ARG(QProcess::ProcessError, QProcess::Crashed));
        }
        QCOMPARE(finished.count(), 1);
        QCOMPARE(manager.activeCount(), 0);
        QCOMPARE(manager.task(id)->state, TaskState::Failed);
    }
    void successfulExitRequiresActualOutput() {
        QTemporaryDir dir;
        DownloadManager manager(nullptr);
        configure(manager);
        const auto id = manager.enqueue(request(dir, "missing-output"));
        manager.start();
        QTRY_COMPARE(manager.task(id)->state, TaskState::Failed);
        QCOMPARE(manager.task(id)->failure.category, DownloadErrorCategory::Disk);
    }
    void parserEventsReachModel_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("stdout") << QString("success");
        QTest::newRow("stderr") << QString("stderr-progress");
    }
    void parserEventsReachModel() {
        QFETCH(QString, mode);
        QTemporaryDir dir;
        DownloadManager manager(nullptr);
        configure(manager);
        const auto id = manager.enqueue(request(dir, mode));
        bool sawProgress = false;
        bool sawPostProcessing = false;
        connect(&manager, &DownloadManager::taskChanged, this, [&](const QString& taskId) {
            const auto* task = manager.task(taskId);
            sawProgress = sawProgress || task->progressPercent == 42.5;
            sawPostProcessing = sawPostProcessing || task->state == TaskState::PostProcessing;
        });
        manager.start();
        QTRY_COMPARE(manager.task(id)->state, TaskState::Completed);
        QVERIFY(sawProgress);
        QVERIFY(sawPostProcessing);
        QCOMPARE(manager.task(id)->progressPercent, 100.0);
        QVERIFY(QFile::exists(manager.task(id)->outputPath));
    }
    void shutdownRecoversUnfinishedWithoutCancelling() {
        QTemporaryDir dir;
        const auto queuePath = dir.filePath("data/queue.json");
        QString runningId;
        QString queuedId;
        {
            DownloadManager manager(nullptr);
            manager.setQueueFile(queuePath);
            configure(manager, 30000);
            manager.setParallelLimit(1);
            runningId = manager.enqueue(request(dir));
            queuedId = manager.enqueue(request(dir));
            manager.start();
            QTRY_COMPARE(manager.runningCount(), 1);
            manager.shutdown();
            QCOMPARE(manager.task(runningId)->state, TaskState::Downloading);
            QCOMPARE(manager.task(queuedId)->state, TaskState::Queued);
        }
        DownloadManager restored(nullptr);
        restored.setQueueFile(queuePath);
        configure(restored);
        QCOMPARE(restored.restoreQueue(), 2);
        QVERIFY(restored.isPaused());
        QCOMPARE(restored.runningCount(), 0);
        QCOMPARE(restored.queuedCount(), 2);
        QCOMPARE(restored.task(runningId)->state, TaskState::Queued);
        QVERIFY(restored.task(runningId)->recovered);
        QCOMPARE(restored.restoreQueue(), 0);
        restored.start();
        QTRY_COMPARE(restored.task(runningId)->state, TaskState::Completed);
        QTRY_COMPARE(restored.task(queuedId)->state, TaskState::Completed);
        QVERIFY(QueueStore(queuePath).load().isEmpty());
    }
    void corruptQueueHasRecoverableBackup() {
        QTemporaryDir dir;
        const auto queuePath = dir.filePath("queue.json");
        QFile queue(queuePath);
        QVERIFY(queue.open(QIODevice::WriteOnly));
        queue.write("broken queue");
        queue.close();
        DownloadManager manager(nullptr);
        manager.setQueueFile(queuePath);
        QSignalSpy diagnostic(&manager, &DownloadManager::storageError);
        QCOMPARE(manager.restoreQueue(), 0);
        QCOMPARE(diagnostic.count(), 1);
        const auto backups = QDir(dir.path()).entryList({"queue.json.recovery-*.json"}, QDir::Files);
        QCOMPARE(backups.size(), 1);
        QFile backup(dir.filePath(backups[0]));
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QCOMPARE(backup.readAll(), QByteArray("broken queue"));
        manager.enqueue(request(dir));
        QCOMPARE(QueueStore(queuePath).load().size(), 1);
    }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() >= 5 && args[1] == "--download-child") {
        auto* progressStream = args[2] == "stderr-progress" ? stderr : stdout;
        std::fputs("download:42.5%|5.1MiB/s|00:12|1024|2048\n", progressStream);
        std::fflush(progressStream);
        QThread::msleep(static_cast<unsigned long>(args[4].toUInt()));
        std::fputs("postprocess:processing\n", progressStream);
        std::fflush(progressStream);
        QThread::msleep(20);
        if (args[2] != "missing-output") {
            QFile file(args[3]);
            if (!file.open(QIODevice::WriteOnly)) return 1;
            file.write("fixture media");
            file.close();
            const auto output = ("vdppath:" + args[3] + "\n").toUtf8();
            std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
            std::fflush(stdout);
        }
        return 0;
    }
    DownloadManagerTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "test_download_manager.moc"
