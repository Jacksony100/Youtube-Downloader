#include "runtime/tool_install.hpp"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
using namespace vdp;

class ToolInstallTests final : public QObject {
    Q_OBJECT
    static bool write(const QString& path, const QByteArray& data) {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }
    static QByteArray read(const QString& path) {
        QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }
private slots:
    void successRemovesBackups() {
        QTemporaryDir temp;
        const auto source = temp.filePath("new"), target = temp.filePath("tool");
        QVERIFY(write(source, "new")); QVERIFY(write(target, "old"));
        QString error;
        QVERIFY2(installToolFiles({{source, target}}, {}, &error), qPrintable(error));
        QCOMPARE(read(target), QByteArray("new"));
        QCOMPARE(QDir(temp.path()).entryList({"*.bak-*", "*.new-*"}, QDir::Files).size(), 0);
    }
    void copyPreparationFailurePreservesOld() {
        QTemporaryDir temp;
        const auto source = temp.filePath("new"), target = temp.filePath("tool");
        QVERIFY(write(source, "new")); QVERIFY(write(target, "old"));
        QVERIFY(write(temp.filePath("not-a-directory"), "file"));
        QString error;
        QVERIFY(!installToolFiles({{source, target}, {source, temp.filePath("not-a-directory/tool")}}, {}, &error));
        QCOMPARE(read(target), QByteArray("old"));
        QVERIFY(!error.isEmpty());
    }
    void postValidationRollback() {
        QTemporaryDir temp;
        const auto source = temp.filePath("new"), target = temp.filePath("tool");
        QVERIFY(write(source, "new")); QVERIFY(write(target, "old"));
        QString error;
        QVERIFY(!installToolFiles({{source, target}}, [target](const QString& path, QString*) { return path != target; }, &error));
        QCOMPARE(read(target), QByteArray("old"));
    }
    void ffmpegPairRollsBackTogether() {
        QTemporaryDir temp;
        const auto source = temp.filePath("new"), ffmpeg = temp.filePath("ffmpeg"), ffprobe = temp.filePath("ffprobe");
        QVERIFY(write(source, "new")); QVERIFY(write(ffmpeg, "old-ffmpeg")); QVERIFY(write(ffprobe, "old-ffprobe"));
        QString error;
        QVERIFY(!installToolFiles({{source, ffmpeg}, {source, ffprobe}}, [ffprobe](const QString& path, QString*) { return path != ffprobe; }, &error));
        QCOMPARE(read(ffmpeg), QByteArray("old-ffmpeg"));
        QCOMPARE(read(ffprobe), QByteArray("old-ffprobe"));
        QCOMPARE(QDir(temp.path()).entryList({"*.bak-*", "*.new-*"}, QDir::Files).size(), 0);
    }
    void manifestFailureRollsBack() {
        QTemporaryDir temp;
        const auto source = temp.filePath("new"), target = temp.filePath("tool");
        QVERIFY(write(source, "new")); QVERIFY(write(target, "old"));
        QVERIFY(!installToolFiles({{source, target}}, {}, nullptr, [](QString*) { return false; }));
        QCOMPARE(read(target), QByteArray("old"));
    }
    void duplicateTargetRejected() {
        QTemporaryDir temp;
        const auto source = temp.filePath("new"), target = temp.filePath("tool");
        QVERIFY(write(source, "new")); QVERIFY(write(target, "old"));
        QVERIFY(!installToolFiles({{source, target}, {source, target}}, {}));
        QCOMPARE(read(target), QByteArray("old"));
    }
};
QTEST_GUILESS_MAIN(ToolInstallTests)
#include "test_tool_install.moc"
