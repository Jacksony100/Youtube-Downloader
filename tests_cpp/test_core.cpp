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
    }

    void friendlyRuntimeError() {
        QCOMPARE(sanitizeError("No supported JavaScript runtime could be found"),
                 QString("Deno не найден. Откройте «Инструменты» и восстановите runtime."));
    }
};

QTEST_MAIN(CoreTests)
#include "test_core.moc"
