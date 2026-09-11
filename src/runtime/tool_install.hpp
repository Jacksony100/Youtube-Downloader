#pragma once

#include <QString>
#include <QVector>
#include <functional>

namespace vdp {
struct InstallEntry {
    QString source;
    QString target;
};
using InstallValidator = std::function<bool(const QString&, QString*)>;
using InstallCommit = std::function<bool(QString*)>;

// All files form one transaction. Call on a worker for large binaries/validators.
bool installToolFiles(const QVector<InstallEntry>& entries, const InstallValidator& validate,
                      QString* error = nullptr, const InstallCommit& commit = {});
}
