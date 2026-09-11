#include "diagnostics/diagnostics.hpp"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
using namespace vdp;
class DiagnosticsTests final : public QObject {
    Q_OBJECT
private slots:
    void redacts() {
        const auto text=redactSecrets("Cookie: abc=PRIVATE\nAuthorization: Bearer PRIVATE\nhttps://user:PRIVATE@host/?token=PRIVATE&signature=PRIVATE\n--cookies \"PRIVATE path\"\napi_key=PRIVATE password=PRIVATE\nBearer PRIVATE");
        QVERIFY(!text.contains("PRIVATE")); QVERIFY(text.contains("[REDACTED]"));
        QVERIFY(!redactSecrets("{\"token\":\"PRIVATE\",\"password\": \"PRIVATE\"}").contains("PRIVATE"));
    }
    void rotation() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        for(int i=0;i<160;++i) QVERIFY(appendBoundedLog(dir.path(),"app",QString(16380,'x')));
        QVERIFY(QFileInfo(QDir(dir.path()).filePath("app.log")).size()<=2*1024*1024);
        QVERIFY(QFile::exists(QDir(dir.path()).filePath("app.log.1")));
        QVERIFY(!appendBoundedLog(dir.path(),"../escape","x"));
    }
};
QTEST_GUILESS_MAIN(DiagnosticsTests)
#include "test_diagnostics.moc"
