#include "main_window.hpp"
#include "downloads/download_manager.hpp"
#include "metadata/metadata_service.hpp"
#include "runtime/toolchain_service.hpp"
#include "runtime/tool_updater.hpp"
#include <QApplication>
#include <QComboBox>
#include <QClipboard>
#include <QCheckBox>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
using namespace vdp;
class WindowTests final : public QObject {
    Q_OBJECT
    QPushButton* button(MainWindow& w,const QString& text) {
        for(auto* b:w.findChildren<QPushButton*>())if(b->text()==text)return b;
        return nullptr;
    }
    QLineEdit* url(MainWindow& w) {
        for(auto* e:w.findChildren<QLineEdit*>())if(e->accessibleName()=="Ссылка на видео")return e;
        return nullptr;
    }
    void setDataRoot(const QString& dir) { qputenv("VDP_DATA_ROOT",dir.toUtf8()); }
private slots:
    void pasteEnqueueSettingsAndHistorySearch() {
        QTemporaryDir dir; setDataRoot(dir.path()); ToolchainManager tools;
        QSettings settings(tools.paths().settingsFile,QSettings::IniFormat);
        settings.setValue("output_dir",dir.path()); settings.sync();
        MainWindow window(nullptr,false); window.show();
        auto* manager=window.findChild<DownloadManager*>();
        manager->setCommandBuilder([](const DownloadTaskData& task){
            return DownloadCommand{QCoreApplication::applicationFilePath(),{"--download-child",task.outputDirectory}};
        });
        ToolchainStatus status;
        for(auto* tool:{&status.ytdlp,&status.deno,&status.ffmpeg,&status.ffprobe}) { tool->exists=true; tool->version="1.0"; }
        window.findChild<ToolchainService*>()->ready(status);
        QApplication::clipboard()->setText("https://example.org/pasted");
        auto* paste=button(window,"Вставить"); QVERIFY(paste); paste->click();
        QCOMPARE(url(window)->text(),QString("https://example.org/pasted"));
        for(auto* combo:window.findChildren<QComboBox*>())if(combo->accessibleName()=="Простой выбор качества")combo->setCurrentIndex(combo->findData("mp3"));
        auto* autoOpen=window.findChild<QCheckBox*>(); QVERIFY(autoOpen); autoOpen->setChecked(true);
        auto* add=button(window,"Добавить в очередь"); QVERIFY(add); add->click();
        QCOMPARE(manager->taskIds().size(),1);
        const auto id=manager->taskIds().first();
        QCOMPARE(manager->task(id)->formatKey,QString("mp3"));
        QCOMPARE(manager->task(id)->outputDirectory,dir.path());
        QVERIFY(url(window)->text().isEmpty()); manager->cancel(id);
        auto* history=window.findChild<QListWidget*>("HistoryList"); QVERIFY(history); QCOMPARE(history->count(),1);
        QLineEdit* search=nullptr;
        for(auto* edit:window.findChildren<QLineEdit*>())if(edit->placeholderText().startsWith("Поиск"))search=edit;
        QVERIFY(search); search->setText("absent-search-value"); QCOMPARE(history->count(),0);
        search->setText("pasted"); QCOMPARE(history->count(),1);
        settings.sync(); QCOMPARE(settings.value("format").toString(),QString("mp3")); QVERIFY(settings.value("auto_open").toBool());
        QTRY_VERIFY(!window.findChild<MetadataService*>()->isBusy()); window.close();
    }
    void previewLoadsActualThumbnailBytes() {
        QTemporaryDir dir; setDataRoot(dir.path()); ToolchainManager tools;
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),tools.ytdlpPath()));
        QImage fixture(32,18,QImage::Format_RGB32); fixture.fill(Qt::green);
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(fixture.save(&buffer,"PNG"));
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server,&QTcpServer::newConnection,this,[&] {
            auto* socket=server.nextPendingConnection();
            connect(socket,&QTcpSocket::readyRead,socket,[socket,png] {
                socket->readAll();
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: "+QByteArray::number(png.size())+"\r\nConnection: close\r\n\r\n"+png);
                socket->disconnectFromHost();
            });
            connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        });
        const auto thumbnail=QString("http://127.0.0.1:%1/thumbnail.png").arg(server.serverPort());
        qputenv("VDP_TEST_THUMBNAIL_URL",thumbnail.toUtf8());
        MainWindow window(nullptr,false); window.show();
        ToolchainStatus status; status.ytdlp={"yt-dlp",tools.ytdlpPath(),"2026.08.19",true,false};
        window.findChild<ToolchainService*>()->ready(status);
        url(window)->setText("https://example.org/thumbnail"); button(window,"Проверить")->click();
        auto* artwork=window.findChild<QLabel*>("PreviewArtwork"); QVERIFY(artwork);
        QTRY_VERIFY_WITH_TIMEOUT(!artwork->pixmap().isNull(),10000);
        QCOMPARE(artwork->pixmap().toImage().pixelColor(5,5),QColor(Qt::green));
        qunsetenv("VDP_TEST_THUMBNAIL_URL"); window.close();
    }
    void retryCannotUnpauseDuringRuntimeUpdate() {
        QTemporaryDir dir; setDataRoot(dir.path());
        MainWindow window(nullptr,false); window.show();
        auto* manager=window.findChild<DownloadManager*>();
        const auto id=manager->enqueue({"https://example.org/retry","Видео",dir.path(),formatPreset("best")});
        manager->cancel(id);
        ToolchainStatus status;
        for(auto* tool:{&status.ytdlp,&status.deno,&status.ffmpeg,&status.ffprobe}) { tool->exists=true; tool->version="1.0"; }
        window.findChild<ToolchainService*>()->ready(status);
        manager->setPaused(true);
        ToolArtifact artifact; artifact.url=QUrl("https://127.0.0.1:9/tool.exe"); artifact.fileName="tool.exe";
        artifact.sha256=QByteArray(64,'1'); artifact.binaries={{"tool.exe","yt-dlp",{"--version"}}};
        auto* updater=window.findChild<ToolUpdater*>();
        QVERIFY(updater->start(ToolUpdateSpec{"yt-dlp",{artifact}})); QVERIFY(updater->isBusy());
        auto* retry=button(window,"Повторить"); QVERIFY(retry); retry->click();
        QVERIFY(manager->isPaused()); QCOMPARE(manager->task(id)->state,TaskState::Cancelled);
        updater->cancel();
        QTRY_VERIFY(!window.findChild<ToolchainService*>()->isBusy());
        window.close();
    }
    void rendersAndCancelsAndReleasesCards() {
        QTemporaryDir dir; setDataRoot(dir.path());
        MainWindow window(nullptr,false); window.show();
        auto* manager=window.findChild<DownloadManager*>(); QVERIFY(manager);
        auto* pages=window.findChild<QStackedWidget*>(); QVERIFY(pages); QCOMPARE(pages->count(),5);
        auto* sidebar=window.findChild<QListWidget*>("SidebarNavigation"); QVERIFY(sidebar);
        for(int i=0;i<5;++i) { sidebar->setCurrentRow(i); QCOMPARE(pages->currentIndex(),i); }
        sidebar->setCurrentRow(0);
        manager->setParallelLimit(1); manager->setCancellationTimeout(20);
        manager->setCommandBuilder([](const DownloadTaskData& task) {
            return DownloadCommand{QCoreApplication::applicationFilePath(),{"--download-child",task.outputDirectory}};
        });
        const auto first=manager->enqueue({"https://example.org/one","Первое видео",dir.path(),formatPreset("best")});
        const auto second=manager->enqueue({"https://example.org/two","Второе видео",dir.path(),formatPreset("mp3")});
        QCOMPARE(manager->queuedCount(),2); manager->cancel(second);
        QCOMPARE(manager->task(second)->state,TaskState::Cancelled);
        manager->start(); QTRY_COMPARE(manager->runningCount(),1);
        manager->cancel(first); QTRY_COMPARE(manager->task(first)->state,TaskState::Cancelled);
        QPointer<QFrame> card;
        for(auto* frame:window.findChildren<QFrame*>())if(frame->property("taskId").toString()==first)card=frame;
        QVERIFY(card); auto* clear=button(window,"Очистить завершённые"); QVERIFY(clear);
        clear->click(); QTRY_VERIFY(card.isNull()); QVERIFY(manager->taskIds().isEmpty());
        auto* history=window.findChild<QListWidget*>("HistoryList"); QVERIFY(history); QCOMPARE(history->count(),2);
        QVERIFY(history->item(0)->text().contains("MSK"));
        const auto evidence=qEnvironmentVariable("VDP_UI_EVIDENCE");
        if(!evidence.isEmpty()) { QDir().mkpath(evidence); QVERIFY(window.grab().save(QDir(evidence).filePath("main-window.png"))); }
        window.close();
    }
    void metadataFormatsAndKeyboard() {
        QTemporaryDir dir; setDataRoot(dir.path()); ToolchainManager tools;
        QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),tools.ytdlpPath()));
        MainWindow window(nullptr,false); window.show();
        ToolchainStatus status;
        status.ytdlp = {"yt-dlp", tools.ytdlpPath(), "2026.08.19", true, false};
        window.findChild<ToolchainService*>()->ready(status);
        auto* input=url(window); QVERIFY(input);
        input->setText("https://example.org/watch");
        auto* check=button(window,"Проверить"); QVERIFY(check); check->click();
        auto* advanced=window.findChild<QComboBox*>("AdvancedFormats"); QVERIFY(advanced);
        QTRY_VERIFY_WITH_TIMEOUT(advanced->isVisible(),10000); QVERIFY(advanced->count()>2);
        bool hasSimple=false;
        for(auto* combo:window.findChildren<QComboBox*>())if(combo->accessibleName()=="Простой выбор качества") { QCOMPARE(combo->count(),5); hasSimple=true; }
        QVERIFY(hasSimple);
        const auto evidence=qEnvironmentVariable("VDP_UI_EVIDENCE");
        if(!evidence.isEmpty()) QVERIFY(window.grab().save(QDir(evidence).filePath("advanced-formats.png")));
        input->setText("https://example.org/new"); QVERIFY(!advanced->isVisible());
        QTest::keyClick(&window,Qt::Key_L,Qt::ControlModifier); QTRY_VERIFY(input->hasFocus());
        window.close();
    }
    void restartRestoresAndSettingsPersist() {
        QTemporaryDir dir; setDataRoot(dir.path()); QString id;
        {
            MainWindow window(nullptr,false);
            auto* manager=window.findChild<DownloadManager*>();
            id=manager->enqueue({"https://example.org/recover","Сохранённое видео",dir.path(),formatPreset("720p")});
            auto* spin=window.findChild<QSpinBox*>(); QVERIFY(spin); spin->setValue(4);
            window.close();
        }
        MainWindow restored(nullptr,false);
        auto* manager=restored.findChild<DownloadManager*>(); QVERIFY(manager->task(id));
        QCOMPARE(manager->task(id)->state,TaskState::Queued); QVERIFY(manager->isPaused());
        QCOMPARE(restored.findChild<QSpinBox*>()->value(),4); restored.close();
    }
};
int main(int argc,char** argv) {
    QApplication app(argc,argv);
#ifdef Q_OS_WIN
    QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/segoeui.ttf");
    app.setFont(QFont("Segoe UI", 10));
#endif
    if(app.arguments().contains("--dump-single-json")) {
        QFile output; output.open(stdout,QIODevice::WriteOnly);
        auto fixture=QJsonDocument::fromJson("{\"title\":\"Fixture video\",\"uploader\":\"Fixture channel\",\"duration\":123,\"formats\":[{\"format_id\":\"137\",\"height\":1080,\"vcodec\":\"avc1\",\"acodec\":\"none\",\"filesize\":10000000},{\"format_id\":\"140\",\"vcodec\":\"none\",\"acodec\":\"aac\"}]}").object();
        if(qEnvironmentVariableIsSet("VDP_TEST_THUMBNAIL_URL"))fixture.insert("thumbnail",qEnvironmentVariable("VDP_TEST_THUMBNAIL_URL"));
        output.write(QJsonDocument(fixture).toJson());
        return 0;
    }
    if(app.arguments().contains("--download-child")) { QTimer::singleShot(10000,&app,&QCoreApplication::quit); return app.exec(); }
    WindowTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "test_main_window.moc"
