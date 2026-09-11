#include "storage/history_repository.hpp"
#include "storage/task_serialization.hpp"
#include "core.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace vdp;

class HistoryRepositoryTests final : public QObject {
    Q_OBJECT
    static DownloadTaskData record(const QString& id, int seconds = 0) {
        DownloadTaskData task;
        task.id = id;
        task.title = "Видео " + id;
        task.url = "https://example.com/watch/" + id;
        task.outputPath = "C:/Downloads/" + id + ".mp4";
        task.outputDirectory = "C:/Downloads";
        task.formatKey = "best";
        task.formatLabel = "Лучшее";
        task.formatSelector = "bestvideo+bestaudio/best";
        task.state = TaskState::Completed;
        task.createdAt = QDateTime::fromString("2026-09-11T00:00:00Z", Qt::ISODate);
        task.updatedAt = task.createdAt.addSecs(seconds);
        return task;
    }
private slots:
    void legacyHistoryMigratesAtomically() {
        QTemporaryDir dir;
        const auto path = dir.filePath("history.json");
        const QJsonArray legacy{QJsonObject{{"id", "old"}, {"title", "Старое видео"},
            {"url", "https://example.com/old"}, {"path", "C:/Downloads/old.mp3"},
            {"format", "MP3"}, {"status", "completed"}, {"error", ""},
            {"created_at", "2026-09-10T21:30:00Z"}}};
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto original = QJsonDocument(legacy).toJson();
        file.write(original);
        file.close();
        HistoryRepository repository(path);
        QString error;
        QVERIFY2(repository.load(&error), qPrintable(error));
        QCOMPARE(repository.newestFirst().size(), 1);
        QCOMPARE(repository.newestFirst()[0].task.formatKey, QString("mp3"));
        QVERIFY(repository.newestFirst()[0].task.extractAudio);
        const auto oldTask = repository.newestFirst()[0].task;
        QCOMPARE(oldTask.audioQuality, QString("192K"));
        FormatPreset oldPreset{oldTask.formatKey, oldTask.formatLabel, oldTask.formatSelector,
            oldTask.extractAudio, oldTask.extension, oldTask.audioQuality, oldTask.originalAudio};
        const auto redownloadArgs = buildDownloadArguments(oldTask.url, oldPreset,
            oldTask.outputDirectory, "ffmpeg", "deno");
        QVERIFY(redownloadArgs.contains("-x"));
        QCOMPARE(redownloadArgs.value(redownloadArgs.indexOf("--audio-format") + 1), QString("mp3"));
        QCOMPARE(redownloadArgs.value(redownloadArgs.indexOf("--audio-quality") + 1), QString("192K"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
        file.close();
        QVERIFY2(repository.append(record("new", 100), &error), qPrintable(error));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto migrated = QJsonDocument::fromJson(file.readAll());
        QCOMPARE(migrated.object().value("schema").toInt(), 1);
        QCOMPARE(migrated.object().value("records").toArray().size(), 2);
        file.close();
        HistoryRepository reloaded(path);
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.newestFirst()[0].task.id, QString("new"));
        QCOMPARE(reloaded.newestFirst()[1].task.url, QString("https://example.com/old"));
        QCOMPARE(reloaded.newestFirst()[1].task.audioQuality, QString("192K"));
    }
    void emptySavedAudioQualityFallsBackWithoutOverridingExplicitQuality() {
        QJsonObject row{{"id", "old"}, {"url", "https://example.com/old"},
            {"format", "MP3"}, {"status", "completed"}, {"audio_quality", ""}};
        const auto empty = storage::taskFromJson(row, true);
        QVERIFY(empty);
        QCOMPARE(empty->audioQuality, QString("192K"));
        row.insert("audio_quality", "320K");
        const auto explicitQuality = storage::taskFromJson(row, true);
        QVERIFY(explicitQuality);
        QCOMPARE(explicitQuality->audioQuality, QString("320K"));
    }
    void searchDeleteClearAndRetention() {
        QTemporaryDir dir;
        HistoryRepository repository(dir.filePath("history.json"));
        for (int i = 0; i < HistoryRepository::maximumRecords + 2; ++i)
            QVERIFY(repository.append(record(QString::number(i), i)));
        QCOMPARE(repository.newestFirst().size(), HistoryRepository::maximumRecords);
        QCOMPARE(repository.newestFirst()[0].task.id, QString("501"));
        QCOMPARE(repository.newestFirst("WATCH/501").size(), 1);
        QVERIFY(repository.remove("501"));
        QVERIFY(repository.newestFirst("WATCH/501").isEmpty());
        QCOMPARE(repository.newestFirst().size(), 499);
        QVERIFY(repository.clear());
        QVERIFY(repository.newestFirst().isEmpty());
        HistoryRepository reloaded(dir.filePath("history.json"));
        QVERIFY(reloaded.load());
        QVERIFY(reloaded.newestFirst().isEmpty());
    }
    void corruptHistoryIsPreserved() {
        QTemporaryDir dir;
        const auto path = dir.filePath("history.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("broken history");
        file.close();
        HistoryRepository repository(path);
        QString error;
        QVERIFY(!repository.load(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!repository.append(record("new"), &error));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("broken history"));
    }
    void failedMutationLeavesMemoryAndDiskIntact() {
        QTemporaryDir dir;
        const auto path = dir.filePath("history.json");
        HistoryRepository repository(path);
        QVERIFY(repository.append(record("kept")));
        QVERIFY(QFile::rename(path, path + ".previous"));
        QVERIFY(QDir().mkpath(path));
        QString error;
        QVERIFY(!repository.remove("kept", &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(repository.newestFirst().size(), 1);
        QVERIFY(QFile::exists(path + ".previous"));
    }
    void retryUpdatesExistingRecord() {
        QTemporaryDir dir;
        HistoryRepository repository(dir.filePath("history.json"));
        auto task = record("same");
        task.state = TaskState::Failed;
        QVERIFY(repository.append(task));
        task.state = TaskState::Completed;
        QVERIFY(repository.append(task));
        QCOMPARE(repository.newestFirst().size(), 1);
        QCOMPARE(repository.newestFirst()[0].task.state, TaskState::Completed);
    }
};

QTEST_GUILESS_MAIN(HistoryRepositoryTests)
#include "test_history_repository.moc"
