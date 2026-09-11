#include "runtime/checksum_parser.hpp"
#include "runtime/toolchain_lock.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
using namespace vdp;

class ChecksumParserTests final : public QObject {
    Q_OBJECT
private slots:
    void exactArtifact() {
        const QString a(64, 'a'), b(64, 'B');
        QCOMPARE(findSha256(a + "  wrong.exe\r\n" + b + " *yt-dlp.exe\r\n", "yt-dlp.exe"),
                 std::optional<QByteArray>(b.toLatin1().toLower()));
        QVERIFY(!findSha256(a + "  other-yt-dlp.exe\n", "yt-dlp.exe"));
        QVERIFY(!findSha256(a + "  nested/yt-dlp.exe\n", "yt-dlp.exe"));
        QVERIFY(!findSha256(a + "  yt-dlp.exe.bak\n", "yt-dlp.exe"));
    }
    void bsdAndSingle() {
        const QString hash(64, 'c');
        QCOMPARE(findSha256("SHA256 (deno.zip) = " + hash, "deno.zip"), std::optional<QByteArray>(hash.toLatin1()));
        QVERIFY(!findSha256("Hash: " + hash, "deno.zip"));
        QCOMPARE(findSha256("Hash: " + hash + "\r\n", "deno.zip", true), std::optional<QByteArray>(hash.toLatin1()));
        QCOMPARE(findSha256(hash + "\n", "ffmpeg.zip", true), std::optional<QByteArray>(hash.toLatin1()));
        QVERIFY(!findSha256(hash, "ffmpeg.zip", false));
        QVERIFY(!findSha256("Hash: " + hash + "\n" + hash + "  wrong.zip", "ffmpeg.zip", true));
        QCOMPARE(findSha256("Algorithm : SHA256\r\nHash      : " + hash +
                            "\r\nPath      : C:\\a\\deno.zip\r\n", "deno.zip", true),
                 std::optional<QByteArray>(hash.toLatin1()));
        QVERIFY(!findSha256("Hash : " + hash + "\nPath : C:\\a\\wrong.zip", "deno.zip", true));
    }
    void malformedAndAmbiguous() {
        QVERIFY(!findSha256(QString(63, 'a') + "  yt-dlp.exe", "yt-dlp.exe"));
        QVERIFY(!findSha256(QString(64, 'g') + "  yt-dlp.exe", "yt-dlp.exe"));
        QVERIFY(!findSha256(QString(64, 'a') + "  yt-dlp.exe\n" + QString(64, 'b') + "  yt-dlp.exe", "yt-dlp.exe"));
        QVERIFY(!findSha256(QString(64, 'a'), "", true));
        QVERIFY(!findSha256(QString(64, 'a'), "../bad.exe", true));
    }
    void repositoryLock() {
        QFile file(QFINDTESTDATA("../runtime/toolchain-lock.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        const auto keys = QJsonDocument::fromJson(bytes).object().value("tools").toObject().keys();
        QVERIFY(keys.contains("ffmpeg-windows-x64"));
        QVERIFY(keys.contains("ffmpeg-macos-x64"));
        for (const auto& key : keys) {
            QString error;
            QVERIFY2(parseToolchainLock(bytes, key, &error).has_value(), qPrintable(key + ": " + error));
        }
        QVERIFY(!parseToolchainLock("{}", "ffmpeg-windows-x64"));
        QVERIFY(!parseToolchainLock(bytes, "missing-platform"));
    }
};
QTEST_GUILESS_MAIN(ChecksumParserTests)
#include "test_checksum_parser.moc"
