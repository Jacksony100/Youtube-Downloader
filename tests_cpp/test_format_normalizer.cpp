#include "metadata/format_normalizer.hpp"
#include <QtTest>
using namespace vdp;
class FormatTests final : public QObject {
    Q_OBJECT
private slots:
    void normalizes() {
        const QJsonArray rows{
            QJsonObject{{"format_id","v1"},{"height",1080},{"fps",60},{"vcodec","avc1.1"},{"acodec","none"},{"filesize",10485760}},
            QJsonObject{{"format_id","v2"},{"height",1080},{"fps",60},{"vcodec","avc1.2"},{"acodec","none"},{"filesize_approx",20971520}},
            QJsonObject{{"format_id","v3"},{"height",2160},{"vcodec","av01.1"},{"acodec","none"}},
            QJsonObject{{"format_id","a1"},{"vcodec","none"},{"acodec","opus"},{"filesize",1024}},
            QJsonObject{{"format_id","storyboard"},{"vcodec","none"},{"acodec","none"}},
            QJsonObject{{"format_id","drm"},{"height",720},{"vcodec","avc1"},{"has_drm",true}}};
        const auto options = normalizeFormats(rows);
        QCOMPARE(options.size(), 5);
        QCOMPARE(options[0].height,2160); QVERIFY(options[0].estimatedBytes < 0);
        QVERIFY(!options[0].label.contains("МБ"));
        QCOMPARE(options[1].id,QString("v2")); QCOMPARE(options[1].estimatedBytes,qint64(20971520));
        QVERIFY(options[1].selector.contains("+bestaudio"));
        QVERIFY(options[2].audioOnly); QCOMPARE(options[4].preset.audioQuality,QString("320K"));
    }
    void preservesPresets() { QVERIFY(normalizeFormats({}).isEmpty()); QCOMPARE(formatPresets().size(),5); }
    void combinedAndExactSize() {
        const auto options=normalizeFormats({QJsonObject{{"format_id","18"},{"height",480},{"vcodec","avc1"},{"acodec","aac"},{"filesize",200},{"filesize_approx",300}}});
        QCOMPARE(options[0].selector,QString("18")); QCOMPARE(options[0].estimatedBytes,qint64(200));
    }
    void metadataPreview() {
        const auto m=parseMetadata(QJsonObject{{"title","Видео"},{"uploader","Канал"},{"duration",42},{"thumbnail","https://example.org/a.webp"},{"thumbnails",QJsonArray{QJsonObject{{"url","https://example.org/a.jpg"}}}}},"https://example.org/watch");
        QCOMPARE(m.sourceHost,QString("example.org")); QCOMPARE(m.durationSeconds,42); QVERIFY(m.thumbnailUrl.endsWith(".jpg"));
    }
    void modernCodecsMergeAudioIntoCompatibleContainer() {
        const auto options = normalizeFormats({
            QJsonObject{{"format_id", "vp9-video"}, {"height", 1440}, {"vcodec", "vp09.00"}, {"acodec", "none"}},
            QJsonObject{{"format_id", "av1-video"}, {"height", 2160}, {"vcodec", "av01.0"}, {"acodec", "none"}},
            QJsonObject{{"format_id", "audio"}, {"vcodec", "none"}, {"acodec", "opus"}}});
        for (const auto& option : options) {
            if (option.audioOnly) continue;
            QCOMPARE(option.extension, QString("mkv"));
            QVERIFY(option.selector.contains("+bestaudio"));
            const auto args = buildDownloadArguments("https://example.org/video", option.preset, "out", "ffmpeg", "deno");
            QCOMPARE(args[args.indexOf("--merge-output-format") + 1], QString("mkv"));
            QCOMPARE(args[args.indexOf("-f") + 1], option.selector);
        }
    }
    void originalAudioExtractsCombinedFallbackWithoutTranscodingRequest() {
        const auto options = normalizeFormats({QJsonObject{{"format_id", "18"}, {"height", 480},
            {"vcodec", "avc1"}, {"acodec", "aac"}}});
        const auto original = options[1];
        QVERIFY(original.audioOnly);
        QVERIFY(original.preset.originalAudio);
        const auto args = buildDownloadArguments("https://example.org/video", original.preset, "out", "ffmpeg", "deno");
        QVERIFY(args.contains("-x"));
        QCOMPARE(args[args.indexOf("--audio-format") + 1], QString("best"));
        QVERIFY(!args.contains("--merge-output-format"));
        QVERIFY(!args.contains("--audio-quality"));
    }
    void unsafeIdsAndDrmDoNotBecomeSelectors() {
        const auto options = normalizeFormats({
            QJsonObject{{"format_id", "video+bad"}, {"height", 1080}, {"vcodec", "avc1"}, {"acodec", "aac"}},
            QJsonObject{{"format_id", "protected"}, {"height", 1080}, {"vcodec", "avc1"}, {"acodec", "aac"}, {"has_drm", true}},
            QJsonObject{{"format_id", "image"}, {"vcodec", "none"}, {"acodec", "none"}}});
        QVERIFY(options.isEmpty());
    }
};
QTEST_GUILESS_MAIN(FormatTests)
#include "test_format_normalizer.moc"
