#include "diagnostics.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSysInfo>

namespace vdp {
QString redactSecrets(const QString& text) {
    QString result = text;
    static const QRegularExpression jsonValues(R"rx((?i)(["'](?:authorization|cookie|access[_-]?token|refresh[_-]?token|api[_-]?key|token|password|secret)["']\s*:\s*["'])[^"']*)rx");
    result.replace(jsonValues, "\\1[REDACTED]");
    static const QRegularExpression headers("(?im)((?:authorization|proxy-authorization|cookie|set-cookie)\\s*:\\s*)[^\\r\\n]+");
    result.replace(headers, "\\1[REDACTED]");
    static const QRegularExpression credentials("(?i)([a-z][a-z0-9+.-]*://)[^\\s/@]+:[^\\s/@]*@");
    result.replace(credentials, "\\1[REDACTED]@");
    static const QRegularExpression values("(?i)((?:access[_-]?token|refresh[_-]?token|api[_-]?key|token|password|passwd|secret|signature|sig|key|auth|cookie)\\s*[=:]\\s*)[^&\\s\\\"'<>]+");
    result.replace(values, "\\1[REDACTED]");
    static const QRegularExpression flags("(?i)(--(?:cookies(?:-from-browser)?|username|password|video-password|add-header|proxy)\\s+)(?:\\\"[^\\\"]*\\\"|'[^']*'|\\S+)");
    result.replace(flags, "\\1[REDACTED]");
    static const QRegularExpression bearer("(?i)\\bBearer\\s+[A-Za-z0-9._~+/-]+=*");
    result.replace(bearer, "Bearer [REDACTED]");
    return result;
}
QString diagnosticReport(const ToolchainStatus& tools, const QString& lastError) {
    QStringList lines{QString("Video Downloader Pro %1").arg(VDP_VERSION),
        QSysInfo::prettyProductName() + " / " + QSysInfo::currentCpuArchitecture(),
        QString("Qt %1").arg(qVersion()), "Метаданные: максимум 3; очередь: schema 1; время интерфейса: MSK (UTC+3)"};
    for (const auto& tool : {tools.ytdlp, tools.deno, tools.ffmpeg, tools.ffprobe})
        lines << QString("%1 | %2 | %3 | integrity: %4").arg(tool.name, tool.version, tool.path, tool.verified ? "verified" : "unknown");
    lines << "Последняя ошибка: " + lastError;
    return redactSecrets(lines.join('\n'));
}
bool appendBoundedLog(const QString& directory, const QString& channel, const QString& message) {
    if (channel != "app" && channel != "downloads" && channel != "runtime") return false;
    if (!QDir().mkpath(directory)) return false;
    const auto path = QDir(directory).filePath(channel + ".log");
    QFile file(path);
    constexpr qint64 limit = 2 * 1024 * 1024;
    const auto data = (QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + " " + redactSecrets(message).left(16384) + '\n').toUtf8();
    if (file.size() + data.size() > limit) {
        const auto backup = path + ".1";
        if (QFile::exists(backup) && !QFile::remove(backup)) return false;
        if (!file.rename(backup)) return false;
        file.setFileName(path);
    }
    return file.open(QIODevice::WriteOnly | QIODevice::Append) && file.write(data) == data.size();
}
}
