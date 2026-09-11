#include "downloads/progress_parser.hpp"
#include <QtTest>

using namespace vdp;

class ProgressParserTests final : public QObject {
    Q_OBJECT
private slots:
    void progressAndPath() {
        const auto result = ProgressParser::parseLine("download:42.5%|5.1MiB/s|00:12|10485760|20971520");
        QVERIFY(result.progress);
        QCOMPARE(result.progress->percent.value(), 42.5);
        QCOMPARE(result.progress->speed, QString("5.1MiB/s"));
        QCOMPARE(result.progress->eta, QString("00:12"));
        QCOMPARE(result.progress->downloadedBytes.value(), qint64(10485760));
        QCOMPARE(result.progress->totalBytes.value(), qint64(20971520));
        QCOMPARE(ProgressParser::parseLine("vdppath:C:/Downloads/Test Video.mp4").finalPath.value(),
            QString("C:/Downloads/Test Video.mp4"));
    }
    void malformedAndMissingOptionalFields() {
        QVERIFY(!ProgressParser::parseLine("garbage").progress);
        QVERIFY(!ProgressParser::parseLine("download:NaN|unknown").progress);
        QVERIFY(!ProgressParser::parseLine("download:inf|unknown").progress);
        QVERIFY(!ProgressParser::parseLine("download:-3%|x|x|-1|-4").progress);
        QVERIFY(!ProgressParser::parseLine("download:101%").progress);
        const auto result = ProgressParser::parseLine("download: 9.5%|NA|NA");
        QVERIFY(result.progress);
        QCOMPARE(result.progress->percent.value(), 9.5);
        QVERIFY(!result.progress->downloadedBytes);
        QVERIFY(!result.progress->totalBytes);
        QVERIFY(!ProgressParser::parseLine("vdppath:").finalPath);
    }
    void splitBuffersAndFlush() {
        ProgressParser parser;
        QVERIFY(parser.feed("download:42").isEmpty());
        auto result = parser.feed(".5%|5.1MiB/s|00:12\r\nvdppath:C:/Down");
        QCOMPARE(result.size(), 1);
        QCOMPARE(result[0].progress->percent.value(), 42.5);
        result = parser.feed("loads/Видео.mp4", true);
        QCOMPARE(result.size(), 1);
        QCOMPARE(result[0].finalPath.value(), QString::fromUtf8("C:/Downloads/Видео.mp4"));
        QVERIFY(parser.feed({}, true).isEmpty());
    }
    void oversizedLinesAreBounded() {
        ProgressParser parser;
        QVERIFY(parser.feed(QByteArray(1024 * 1024, 'x')).isEmpty());
        const auto result = parser.feed("\ndownload:1%\n");
        QCOMPARE(result.size(), 1);
        QCOMPARE(result[0].progress->percent.value(), 1.0);
    }
    void detectsPostProcessing() {
        QVERIFY(ProgressParser::parseLine("[Merger] Merging formats").postProcessing);
        QVERIFY(ProgressParser::parseLine("[ExtractAudio] Destination: a.mp3").postProcessing);
        QVERIFY(ProgressParser::parseLine("postprocess:video").postProcessing);
    }
};

QTEST_GUILESS_MAIN(ProgressParserTests)
#include "test_progress_parser.moc"
