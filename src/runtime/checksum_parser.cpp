#include "checksum_parser.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QRegularExpression>

namespace vdp {
std::optional<QByteArray> findSha256(const QString& text, const QString& expectedFileName,
                                   bool singleArtifactDocument) {
    if (expectedFileName.isEmpty() || expectedFileName.contains('/') || expectedFileName.contains('\\'))
        return std::nullopt;
    static const QRegularExpression gnu("^([0-9a-fA-F]{64})[ \\t]+\\*?(.+)$");
    static const QRegularExpression bsd("^SHA256 \\((.+)\\) = ([0-9a-fA-F]{64})$");
    static const QRegularExpression single("^(?:Hash[ \\t]*:[ \\t]*)?([0-9a-fA-F]{64})$",
                                           QRegularExpression::CaseInsensitiveOption);
    std::optional<QByteArray> selected;
    std::optional<QByteArray> singleCandidate;
    bool namedEntries = false;
    const auto lines = text.split('\n');
    int nonemptyLines = 0;
    for (const auto& raw : lines) if (!raw.trimmed().isEmpty()) ++nonemptyLines;
    for (const auto& raw : lines) {
        QString line = raw;
        if (line.endsWith('\r')) line.chop(1);
        QByteArray digest;
        const auto gnuMatch = gnu.match(line);
        const auto bsdMatch = bsd.match(line);
        namedEntries = namedEntries || gnuMatch.hasMatch() || bsdMatch.hasMatch();
        if (gnuMatch.hasMatch() && gnuMatch.captured(2) == expectedFileName)
            digest = gnuMatch.captured(1).toLatin1().toLower();
        else if (bsdMatch.hasMatch() && bsdMatch.captured(1) == expectedFileName)
            digest = bsdMatch.captured(2).toLatin1().toLower();
        else if (singleArtifactDocument) {
            const auto match = single.match(line.trimmed());
            if (match.hasMatch() && (nonemptyLines == 1 || line.trimmed().startsWith("Hash", Qt::CaseInsensitive))) {
                const auto value = match.captured(1).toLatin1().toLower();
                if (singleCandidate && *singleCandidate != value) return std::nullopt;
                singleCandidate = value;
            }
            if (line.trimmed().startsWith("Path", Qt::CaseInsensitive)) {
                QString path = line.section(':', 1).trimmed();
                path.replace('\\', '/');
                if (path.section('/', -1) != expectedFileName) return std::nullopt;
            }
        }
        if (digest.isEmpty()) continue;
        if (selected && *selected != digest) return std::nullopt;
        selected = digest;
    }
    return selected ? selected : (namedEntries ? std::nullopt : singleCandidate);
}

QByteArray fileSha256(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result().toHex();
}
}
