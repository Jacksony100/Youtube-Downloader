#include "core.hpp"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

using namespace vdp;

class CoreTests final : public QObject {
    Q_OBJECT

private slots:
    void comparesToolVersions() {
        QVERIFY(isVersionNewer("2026.08.19", "2026.07.04"));
        QVERIFY(isVersionNewer("2.9.5", "2.3.0"));
        QVERIFY(!isVersionNewer("2026.07.04", "2026.08.19"));
        QVERIFY(!isVersionNewer("2026.08.19", "2026.08.19"));
        QVERIFY(!isVersionNewer("unknown", "2026.08.19"));
        QVERIFY(isVersionNewer("2026.08.19", "unknown"));
    }

    void formatFallback() {
        QCOMPARE(formatPreset("missing").key, QString("best"));
        QVERIFY(formatPreset("mp3").extractAudio);
    }

    void metadataUsesDeno() {
        const auto args = buildMetadataArguments("https://youtu.be/example", "C:/runtime/deno.exe");
        QCOMPARE(args.at(args.indexOf("--js-runtimes") + 1),
                 QString("deno:") + QDir::toNativeSeparators("C:/runtime/deno.exe"));
    }

    void downloadUsesFfmpegAndDeno() {
        const auto args = buildDownloadArguments("https://youtu.be/example", formatPreset("best"),
            "C:/Downloads", "C:/runtime/ffmpeg", "C:/runtime/deno.exe");
        QVERIFY(args.contains("--ffmpeg-location"));
        QVERIFY(args.contains("--js-runtimes"));
        for (const auto& flag : {"--ignore-config", "--no-playlist", "--progress-template"}) QVERIFY(args.contains(flag));
        QVERIFY(args.contains("--progress"));
        QVERIFY(args[args.indexOf("--progress-template") + 1].startsWith("download:download:"));
        QVERIFY(args.contains("postprocess:postprocess:%(progress.status)s"));
        QCOMPARE(args.at(args.indexOf("--print")+1), QString("after_move:vdppath:%(filepath)s"));
        QCOMPARE(args.last(),QString("https://youtu.be/example"));
    }

    void audioArguments() {
        auto preset = formatPreset("mp3"); preset.audioQuality = "320K";
        const auto args=buildDownloadArguments("https://example.org/",preset,"C:/Downloads","ffmpeg","deno");
        QVERIFY(args.contains("-x")); QCOMPARE(args[args.indexOf("--audio-format")+1],QString("mp3"));
        QCOMPARE(args[args.indexOf("--audio-quality")+1],QString("320K"));
        preset={"original","Original","bestaudio/best",false,""};
        const auto original=buildDownloadArguments("https://example.org/",preset,"out","ffmpeg","deno");
        QVERIFY(!original.contains("--merge-output-format"));
    }

    void friendlyErrors_data() {
        QTest::addColumn<QString>("input"); QTest::addColumn<QString>("expected");
        QTest::newRow("403") << "HTTP Error 403: Forbidden" << "403";
        QTest::newRow("429") << "HTTP Error 429: Too many requests" << "ограничила";
        QTest::newRow("private") << "Private video" << "приватное";
        QTest::newRow("unavailable") << "Video unavailable" << "недоступно";
        QTest::newRow("network") << "Connection timeout" << "Сетевая";
        QTest::newRow("geo") << "not available in your region" << "регион";
        QTest::newRow("empty") << "" << "Неизвестная";
    }
    void friendlyErrors() {
        QFETCH(QString,input); QFETCH(QString,expected);
        QVERIFY2(sanitizeError(input).contains(expected,Qt::CaseInsensitive),qPrintable(sanitizeError(input)));
    }
    void secretsNeverInUserErrors() {
        QVERIFY(!sanitizeError("Unexpected: https://user:PRIVATE@example.org?token=PRIVATE").contains("PRIVATE"));
    }

    void friendlyRuntimeError() {
        QCOMPARE(sanitizeError("No supported JavaScript runtime could be found"),
                 QString("Deno не найден. Откройте «Инструменты» и восстановите runtime."));
    }
};

QTEST_MAIN(CoreTests)
#include "test_core.moc"
