#include "metadata/metadata_service.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QtTest>
using namespace vdp;
class MetadataTests final : public QObject {
    Q_OBJECT
    MetadataCommand command(const QString& url) { return {QCoreApplication::applicationFilePath(),{"--metadata-child",url}}; }
private slots:
    void busyIncludesWaitingJobs() {
        MetadataService service([&](const QString& u){return command(u);});
        int completed=0;
        connect(&service,&MetadataService::ready,this,[&] {
            ++completed;
            QCOMPARE(service.isBusy(),completed<10);
        });
        for(int i=0;i<10;++i)service.request(QString::number(i),QString::number(i));
        QVERIFY(service.isBusy()); QCOMPARE(service.activeCount(),3);
        QTRY_COMPARE_WITH_TIMEOUT(completed,10,15000); QVERIFY(!service.isBusy());
    }
    void boundedAndCached() {
        int starts=0;
        MetadataService service([&](const QString& u){ ++starts; return command(u); });
        QSignalSpy ready(&service,&MetadataService::ready);
        for(int i=0;i<10;++i) service.request(QString::number(i),"https://example.org/"+QString::number(i));
        QCOMPARE(service.activeCount(),3);
        service.request("duplicate","https://example.org/0");
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(),11,15000);
        QCOMPARE(starts,10); QCOMPARE(service.activeCount(),0);
        service.request("cached","https://example.org/0");
        QTRY_COMPARE(ready.size(),12); QCOMPARE(starts,10);
    }
    void failureReleasesCapacity() {
        MetadataService service([&](const QString& u){return command(u);});
        QSignalSpy failed(&service,&MetadataService::failed); QSignalSpy ready(&service,&MetadataService::ready);
        service.request("bad","fail");
        for(int i=0;i<4;++i)service.request(QString::number(i),QString::number(i));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(),1,10000); QTRY_COMPARE_WITH_TIMEOUT(ready.size(),4,10000);
        QCOMPARE(service.activeCount(),0);
    }
    void failedStartAndTimeout() {
        MetadataService missing([](const QString&){return MetadataCommand{"/no-such-vdp-program",{}};});
        QSignalSpy failure(&missing,&MetadataService::failed); missing.request("id","url");
        QTRY_COMPARE(failure.size(),1); QCOMPARE(missing.activeCount(),0);
        MetadataService slow([&](const QString& u){return command(u);}); slow.setTimeout(20);
        QSignalSpy timed(&slow,&MetadataService::failed); slow.request("id","slow");
        QTRY_COMPARE(timed.size(),1); QCOMPARE(slow.activeCount(),0);
    }
    void cancelledSubscriberAndDestruction() {
        auto* service=new MetadataService([&](const QString& u){return command(u);});
        QSignalSpy ready(service,&MetadataService::ready);
        service->request("id","url"); service->cancel("id");
        QTRY_COMPARE(service->activeCount(),0); QCOMPARE(ready.size(),0);
        service->request("id","slow"); delete service;
    }
    void cacheCancellationAndReplacementDoNotDeliverStaleData() {
        MetadataService service([&](const QString& url) { return command(url); });
        QSignalSpy ready(&service, &MetadataService::ready);
        service.request("prime", "cached");
        QTRY_COMPARE(ready.size(), 1);
        service.request("cancelled", "cached");
        service.cancel("cancelled");
        service.request("reused", "cached");
        service.request("reused", "replacement");
        QTRY_COMPARE(ready.size(), 2);
        QCOMPARE(ready[1][0].toString(), QString("reused"));
        QCOMPARE(qvariant_cast<VideoMetadata>(ready[1][1]).title, QString("replacement"));
        QTest::qWait(30);
        QCOMPARE(ready.size(), 2);
    }
    void cancelledPendingWorkNeverStartsAndOtherWorkCompletes() {
        QStringList started;
        MetadataService service([&](const QString& url) { started.append(url); return command(url); });
        QSignalSpy ready(&service, &MetadataService::ready);
        for (int i = 0; i < 3; ++i) service.request(QString::number(i), QString::number(i));
        service.request("cancelled", "unused");
        service.cancel("cancelled");
        service.request("last", "last");
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 4, 10000);
        QCOMPARE(started.size(), 4);
        QVERIFY(!started.contains("unused"));
        QVERIFY(started.contains("last"));
    }
    void timeoutReleasesOnlyStoppedProcessesAndKeepsDelivering() {
        MetadataService service([&](const QString& url) { return command(url); });
        service.setTimeout(25);
        QSignalSpy failed(&service, &MetadataService::failed);
        for (int i = 0; i < 7; ++i) service.request(QString::number(i), "slow" + QString::number(i));
        int maximumPhysical = 0;
        QTimer monitor;
        connect(&monitor, &QTimer::timeout, this, [&] {
            int physical = 0;
            for (auto* process : service.findChildren<QProcess*>())
                if (process->state() != QProcess::NotRunning) ++physical;
            maximumPhysical = qMax(maximumPhysical, physical);
            QVERIFY(physical <= 3);
        });
        monitor.start(1);
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 7, 10000);
        QCOMPARE(service.activeCount(), 0);
        for (auto* process : service.findChildren<QProcess*>()) QCOMPARE(process->state(), QProcess::NotRunning);
        QVERIFY(maximumPhysical <= 3);
    }
    void subscriberCallbacksMayCancelOrReplaceOtherSubscribers() {
        MetadataService service([&](const QString& url) { return command(url); });
        int deliveries = 0;
        connect(&service, &MetadataService::ready, this, [&](const QString& id, const VideoMetadata&) {
            ++deliveries;
            service.cancel(id == "first" ? "second" : "first");
        });
        service.request("first", "same");
        service.request("second", "same");
        QTRY_COMPARE(service.activeCount(), 0);
        QCOMPARE(deliveries, 1);
    }
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().contains("--metadata-child")) {
        const auto url=app.arguments().last();
        QTimer::singleShot(url.startsWith("slow")?5000:120,&app,[&app,url]{
            QFile output; output.open(stdout,QIODevice::WriteOnly);
            output.write(url=="fail" ? QByteArray("invalid")
                : QJsonDocument(QJsonObject{{"title", url}, {"formats", QJsonArray{}}}).toJson());
            output.flush(); app.exit(url=="fail"?1:0);
        });
        return app.exec();
    }
    MetadataTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "test_metadata_service.moc"
