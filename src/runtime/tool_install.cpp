#include "tool_install.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QUuid>

namespace vdp {
bool installToolFiles(const QVector<InstallEntry>& entries, const InstallValidator& validate,
                      QString* error, const InstallCommit& commit) {
    struct Replacement { QString target, staged, backup; bool backedUp = false, installed = false; };
    QVector<Replacement> files;
    QSet<QString> targets;
    auto fail = [&](const QString& reason) {
        QString rollbackError;
        for (auto it = files.rbegin(); it != files.rend(); ++it) {
            if (it->installed && !QFile::remove(it->target))
                rollbackError += " Не удалось убрать новый файл: " + it->target;
            if (it->backedUp && !QFile::rename(it->backup, it->target))
                rollbackError += " Не удалось восстановить резервную копию: " + it->backup;
            QFile::remove(it->staged);
        }
        if (error) *error = reason + rollbackError;
        return false;
    };
    if (entries.isEmpty()) return fail("Нет файлов для установки runtime");
    for (const auto& entry : entries) {
        const QString target = QFileInfo(entry.target).absoluteFilePath();
#ifdef Q_OS_WIN
        const QString identity = target.toCaseFolded();
#else
        const QString identity = target;
#endif
        if (targets.contains(identity)) return fail("Повторяющийся путь runtime");
        targets.insert(identity);
        QString validationError;
        if (!QFileInfo(entry.source).isFile() || QFileInfo(entry.source).size() <= 0 ||
            (validate && !validate(entry.source, &validationError)))
            return fail("Проверка нового runtime не пройдена. " + validationError);
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) return fail("Не удалось создать каталог runtime");
        const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
        Replacement file{target, target + ".new-" + suffix, target + ".bak-" + suffix};
        files.append(file);
        if (!QFile::copy(entry.source, file.staged)) return fail("Не удалось подготовить runtime: " + target);
        if (!QFile::setPermissions(file.staged, QFile::permissions(file.staged) | QFileDevice::ExeOwner |
                QFileDevice::ExeUser | QFileDevice::ExeGroup | QFileDevice::ExeOther))
            return fail("Не удалось установить права runtime");
    }
    for (auto& file : files) {
        if (QFileInfo::exists(file.target)) {
            if (!QFile::rename(file.target, file.backup)) return fail("Runtime занят: " + file.target);
            file.backedUp = true;
        }
        if (!QFile::rename(file.staged, file.target)) return fail("Не удалось заменить runtime: " + file.target);
        file.installed = true;
    }
    for (const auto& file : files) {
        QString validationError;
        if (validate && !validate(file.target, &validationError))
            return fail("Установленный runtime не прошёл проверку. " + validationError);
    }
    QString commitError;
    if (commit && !commit(&commitError)) return fail("Не удалось сохранить manifest. " + commitError);
    for (const auto& file : files) if (file.backedUp) QFile::remove(file.backup);
    return true;
}
}
