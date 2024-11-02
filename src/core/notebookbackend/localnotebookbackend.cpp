#include "localnotebookbackend.h"

#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QJsonObject>
#include <QJsonDocument>

#include <vtextedit/textutils.h>

#include <utils/pathutils.h>
#include <utils/fileutils.h>
#include "exception.h"
#include "coreconfig.h"
#include "configmgr.h"

using namespace vnotex;

LocalNotebookBackend::LocalNotebookBackend(const QString &p_name,
                                           const QString &p_displayName,
                                           const QString &p_description,
                                           const QString &p_rootPath,
                                           QObject *p_parent)
    : INotebookBackend(p_rootPath, p_parent),
      m_info(p_name, p_displayName, p_description)
{
}

QString LocalNotebookBackend::getName() const
{
    return m_info.m_name;
}

QString LocalNotebookBackend::getDisplayName() const
{
    return m_info.m_displayName;
}

QString LocalNotebookBackend::getDescription() const
{
    return m_info.m_description;
}

bool LocalNotebookBackend::isEmptyDir(const QString &p_dirPath) const
{
    return PathUtils::isEmptyDir(getFullPath(p_dirPath));
}

void LocalNotebookBackend::makePath(const QString &p_dirPath)
{
    constrainPath(p_dirPath);
    QDir dir(getRootPath());
    if (!dir.mkpath(p_dirPath)) {
        Exception::throwOne(Exception::Type::FailToCreateDir,
                            QStringLiteral("fail to create directory: %1").arg(p_dirPath));
    }
}

void LocalNotebookBackend::writeFile(const QString &p_filePath, const QByteArray &p_data)
{
    const auto filePath = getFullPath(p_filePath);
    FileUtils::writeFile(filePath, p_data);
}

void LocalNotebookBackend::writeFile(const QString &p_filePath, const QString &p_text)
{
    const auto filePath = getFullPath(p_filePath);
    FileUtils::writeFile(filePath, p_text);
}

void LocalNotebookBackend::writeFile(const QString &p_filePath, const QJsonObject &p_jobj)
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
    auto data = QJsonDocument(p_jobj).toJson();
    vte::LineEnding before = vte::LineEnding::LF;
    QString text;
    switch (coreConfig.getLineEndingPolicy()) {
    case LineEndingPolicy::Platform:
#if defined(Q_OS_WIN)
        text = QString::fromUtf8(data);
        vte::TextUtils::transformLineEnding(text, before, vte::LineEnding::CRLF);
#endif
        break;

    case LineEndingPolicy::File:
        // Not supported.
        Q_FALLTHROUGH();
    case LineEndingPolicy::LF:
        break;

    case LineEndingPolicy::CRLF:
        text = QString::fromUtf8(data);
        vte::TextUtils::transformLineEnding(text, before, vte::LineEnding::CRLF);
        break;

    case LineEndingPolicy::CR:
        text = QString::fromUtf8(data);
        vte::TextUtils::transformLineEnding(text, before, vte::LineEnding::CR);
        break;
    }

    if (!text.isEmpty()) {
        writeFile(p_filePath, text);
    } else {
        writeFile(p_filePath, data);
    }
}

QString LocalNotebookBackend::readTextFile(const QString &p_filePath)
{
    const auto filePath = getFullPath(p_filePath);
    return FileUtils::readTextFile(filePath);
}

QByteArray LocalNotebookBackend::readFile(const QString &p_filePath)
{
    const auto filePath = getFullPath(p_filePath);
    return FileUtils::readFile(filePath);
}

bool LocalNotebookBackend::exists(const QString &p_path) const
{
    return QFileInfo::exists(getFullPath(p_path));
}

bool LocalNotebookBackend::existsFile(const QString &p_path) const
{
    QFileInfo fi(getFullPath(p_path));
    return fi.exists() && fi.isFile();
}

bool LocalNotebookBackend::existsDir(const QString &p_path) const
{
    QFileInfo fi(getFullPath(p_path));
    return fi.exists() && fi.isDir();
}

bool LocalNotebookBackend::childExistsCaseInsensitive(const QString &p_dirPath, const QString &p_name) const
{
    return FileUtils::childExistsCaseInsensitive(getFullPath(p_dirPath), p_name);
}

bool LocalNotebookBackend::isFile(const QString &p_path) const
{
    QFileInfo fi(getFullPath(p_path));
    return fi.isFile();
}

void LocalNotebookBackend::renameFile(const QString &p_filePath, const QString &p_name)
{
    Q_ASSERT(isFile(p_filePath));
    const auto filePath = getFullPath(p_filePath);
    FileUtils::renameFile(filePath, p_name);
}

void LocalNotebookBackend::renameDir(const QString &p_dirPath, const QString &p_name)
{
    Q_ASSERT(!isFile(p_dirPath));
    const auto dirPath = getFullPath(p_dirPath);
    FileUtils::renameFile(dirPath, p_name);
}

void LocalNotebookBackend::copyFile(const QString &p_filePath, const QString &p_destPath, bool p_move)
{
    auto filePath = p_filePath;
    if (QFileInfo(filePath).isRelative()) {
        filePath = getFullPath(filePath);
    }

    Q_ASSERT(QFileInfo(filePath).isFile());

    FileUtils::copyFile(filePath, getFullPath(p_destPath), p_move);
}

void LocalNotebookBackend::copyDir(const QString &p_dirPath, const QString &p_destPath, bool p_move)
{
    auto dirPath = p_dirPath;
    if (QFileInfo(dirPath).isRelative()) {
        dirPath = getFullPath(dirPath);
    }

    Q_ASSERT(QFileInfo(dirPath).isDir());

    FileUtils::copyDir(dirPath, getFullPath(p_destPath), p_move);
}

void LocalNotebookBackend::removeFile(const QString &p_filePath)
{
    Q_ASSERT(isFile(p_filePath));
    FileUtils::removeFile(getFullPath(p_filePath));
}

bool LocalNotebookBackend::removeDirIfEmpty(const QString &p_dirPath)
{
    Q_ASSERT(!isFile(p_dirPath));
    return FileUtils::removeDirIfEmpty(getFullPath(p_dirPath));
}

void LocalNotebookBackend::removeDir(const QString &p_dirPath)
{
    Q_ASSERT(!isFile(p_dirPath));
    return FileUtils::removeDir(getFullPath(p_dirPath));
}

QString LocalNotebookBackend::renameIfExistsCaseInsensitive(const QString &p_path) const
{
    return FileUtils::renameIfExistsCaseInsensitive(getFullPath(p_path));
}

void LocalNotebookBackend::addFile(const QString &p_path)
{
    Q_UNUSED(p_path);
    // Do nothing for now.
}

void LocalNotebookBackend::removeEmptyDir(const QString &p_dirPath)
{
    FileUtils::removeEmptyDir(getFullPath(p_dirPath));
}
