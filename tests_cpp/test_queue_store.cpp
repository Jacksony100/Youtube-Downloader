#include "storage/queue_store.hpp"
#include "storage/task_serialization.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace vdp;

class QueueStoreTests final : public QObject {
    Q_OBJECT
    static DownloadTaskData makeTask(const QString& id, TaskState state) {
        DownloadTaskData task;
        task.id = id;
        task.url = "https://example.com/video/" + id;
        task.outputDirectory = "C:/Downloads";
        task.title = "Тест " + id;
        task.state = state;
        task.formatKey = "advanced";
        task.formatLabel = "Видео 1080p";
        task.formatSelector = "137+bestaudio/best";
        task.audioQuality = "320K";
        task.createdAt = task.updatedAt = QDateTime::fromString("2026-09-11T08:00:00.000Z", Qt::ISODateWithMs);
        return task;
    }
private slots:
    void roundTripAndRecovery() {
        QTemporaryDir dir;
        QueueStore store(dir.filePath("queue.json"));
        QString error;
        QVERIFY2(store.save({makeTask("queued", TaskState::Queued), makeTask("preparing", TaskState::Preparing),
            makeTask("running", TaskState::Downloading), makeTask("post", TaskState::PostProcessing),
            makeTask("complete", TaskState::Completed), makeTask("cancelled", TaskState::Cancelled),
            makeTask("failed", TaskState::Failed)}, &error), qPrintable(error));
        const auto tasks = store.load(&error);
        QVERIFY(error.isEmpty());
        QCOMPARE(tasks.size(), 4);
        QVERIFY(!tasks[0].recovered);
        for (const auto& task : tasks) {
            QCOMPARE(task.state, TaskState::Queued);
            QCOMPARE(task.formatSelector, QString("137+bestaudio/best"));
            QCOMPARE(task.audioQuality, QString("320K"));
            QCOMPARE(task.createdAt.timeSpec(), Qt::UTC);
        }
        QVERIFY(tasks[1].recovered);
        QVERIFY(tasks[2].recovered);
        QVERIFY(tasks[3].recovered);
    }
    void cancellationIntentIsNotRecovered() {
        QTemporaryDir dir;
        QueueStore store(dir.filePath("queue.json"));
        auto task = makeTask("cancel", TaskState::Downloading);
        task.cancellationRequested = true;
        QVERIFY(store.save({task}));
        QVERIFY(store.load().isEmpty());
    }
    void corruptAndUnsupportedSchema_data() {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("corrupt") << QByteArray("not JSON");
        QTest::newRow("missing schema") << QByteArray("{\"tasks\":[]}");
        QTest::newRow("future schema") << QByteArray("{\"schema\":2,\"tasks\":[]}");
        QTest::newRow("array") << QByteArray("[]");
        QTest::newRow("missing tasks") << QByteArray("{\"schema\":1}");
    }
    void corruptAndUnsupportedSchema() {
        QFETCH(QByteArray, json);
        QTemporaryDir dir;
        const auto path = dir.filePath("queue.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(json);
        file.close();
        QString error;
        QVERIFY(QueueStore(path).load(&error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), json);
    }
    void invalidAndDuplicateRecordsAreSkipped() {
        QTemporaryDir dir;
        const auto path = dir.filePath("queue.json");
        const auto valid = storage::taskToJson(makeTask("valid", TaskState::Queued));
        const auto terminal = storage::taskToJson(makeTask("done", TaskState::Completed));
        QVERIFY(storage::writeJsonAtomically(path, QJsonDocument(QJsonObject{{"schema", 1},
            {"tasks", QJsonArray{valid, valid, QJsonObject{}, terminal}}}).toJson(), nullptr));
        QString error;
        QCOMPARE(QueueStore(path).load(&error).size(), 1);
        QVERIFY(!error.isEmpty());
    }
    void failedSaveDoesNotReplaceExistingFile() {
        QTemporaryDir dir;
        const auto path = dir.filePath("queue.json");
        QueueStore store(path);
        QVERIFY(store.save({makeTask("kept", TaskState::Queued)}));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto before = file.readAll();
        file.close();
        QString error;
        QVERIFY(!storage::writeJsonAtomically(path + "/child.json", "new", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), before);
    }
};

QTEST_GUILESS_MAIN(QueueStoreTests)
#include "test_queue_store.moc"
