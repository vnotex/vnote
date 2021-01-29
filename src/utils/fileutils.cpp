#include "fileutils.h"

#include <QFile>
#include <QMimeDatabase>
#include <QDateTime>
#include <QTemporaryFile>

#include "../core/exception.h"
#include "pathutils.h"

using namespace vnotex;

QByteArray FileUtils::readFile(const QString &p_filePath)
{
    QFile file(p_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        Exception::throwOne(Exception::Type::FailToReadFile,
                            QString("failed to read file: %1").arg(p_filePath));
    }

    return file.readAll();
}

QString FileUtils::readTextFile(const QString &p_filePath)
{
    QFile file(p_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Exception::throwOne(Exception::Type::FailToReadFile,
                            QString("failed to read file: %1").arg(p_filePath));
    }

    QString text(file.readAll());
    file.close();
    return text;
}

void FileUtils::writeFile(const QString &p_filePath, const QByteArray &p_data)
{
    QFile file(p_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        Exception::throwOne(Exception::Type::FailToWriteFile,
                            QString("failed to write to file: %1").arg(p_filePath));
    }

    file.write(p_data);
    file.close();
}

void FileUtils::writeFile(const QString &p_filePath, const QString &p_text)
{
    QFile file(p_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Exception::throwOne(Exception::Type::FailToWriteFile,
                            QString("failed to write to file: %1").arg(p_filePath));
    }

    QTextStream stream(&file);
    stream << p_text;
    file.close();
}

void FileUtils::renameFile(const QString &p_path, const QString &p_name)
{
    Q_ASSERT(PathUtils::isLegalFileName(p_name));
    QString newFilePath(PathUtils::concatenateFilePath(PathUtils::parentDirPath(p_path), p_name));
    QFile file(p_path);
    if (!file.exists() || !file.rename(newFilePath)) {
        Exception::throwOne(Exception::Type::FailToRenameFile,
                            QString("failed to rename file: %1").arg(p_path));
    }
}

bool FileUtils::childExistsCaseInsensitive(const QString &p_dirPath, const QString &p_name)
{
    QDir dir(p_dirPath);
    if (!dir.exists()) {
        return false;
    }

    auto name = p_name.toLower();
    auto children = dir.entryList(QDir::Dirs | QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const auto &child : children) {
        if (child.toLower() == name) {
            return true;
        }
    }

    return false;
}

bool FileUtils::existsCaseInsensitive(const QString &p_path)
{
    return childExistsCaseInsensitive(PathUtils::parentDirPath(p_path), PathUtils::fileName(p_path));
}

void FileUtils::copyFile(const QString &p_filePath,
                         const QString &p_destPath,
                         bool p_move)
{
    if (PathUtils::areSamePaths(p_filePath, p_destPath)) {
        return;
    }

    QDir dir;
    if (!dir.mkpath(PathUtils::parentDirPath(p_destPath))) {
        Exception::throwOne(Exception::Type::FailToCreateDir,
            QString("failed to create directory: %1").arg(PathUtils::parentDirPath(p_destPath)));
    }

    bool failed = false;
    if (p_move) {
        QFile file(p_filePath);
        if (!file.rename(p_destPath)) {
            failed = true;
        }
    } else {
        if (!QFile::copy(p_filePath, p_destPath)) {
            failed = true;
        }
    }

    if (failed) {
        Exception::throwOne(Exception::Type::FailToCopyFile,
                            QString("failed to copy file: %1 %2").arg(p_filePath, p_destPath));
    }
}

void FileUtils::copyDir(const QString &p_dirPath,
                        const QString &p_destPath,
                        bool p_move)
{
    if (PathUtils::areSamePaths(p_dirPath, p_destPath)) {
        return;
    }

    if (QFileInfo::exists(p_destPath)) {
        Exception::throwOne(Exception::Type::FailToCopyDir,
                            QString("target directory %1 already exists").arg(p_destPath));
    }

    // QDir.rename() could not move directory across dirves.

    // Create target directory.
    QDir destDir(p_destPath);
    if (!destDir.mkpath(p_destPath)) {
        Exception::throwOne(Exception::Type::FailToCreateDir,
            QString("failed to create directory: %1").arg(p_destPath));
    }

    // Copy directory contents recursively.
    QDir srcDir(p_dirPath);
    auto nodes = srcDir.entryInfoList(QDir::Dirs
                                      | QDir::Files
                                      | QDir::Hidden
                                      | QDir::NoSymLinks
                                      | QDir::NoDotAndDotDot);
    for (const auto &node : nodes) {
        auto name = node.fileName();
        if (node.isDir()) {
            copyDir(srcDir.filePath(name), destDir.filePath(name), p_move);
        } else {
            Q_ASSERT(node.isFile());
            copyFile(srcDir.filePath(name), destDir.filePath(name), p_move);
        }
    }

    if (p_move) {
        if (!destDir.rmdir(p_dirPath)) {
            Exception::throwOne(Exception::Type::FailToRemoveDir,
                QString("failed to remove source directory after move: %1").arg(p_dirPath));
        }
    }
}

QString FileUtils::renameIfExistsCaseInsensitive(const QString &p_path)
{
    QFileInfo fi(p_path);
    auto dirPath = fi.absolutePath();
    auto baseName = fi.completeBaseName();
    auto suffix = fi.suffix();
    auto name = fi.fileName();
    int idx = 1;
    while (childExistsCaseInsensitive(dirPath, name)) {
        name = QString("%1_%2").arg(baseName, QString::number(idx));
        if (!suffix.isEmpty()) {
            name += QStringLiteral(".") + suffix;
        }

        ++idx;
    }

    return PathUtils::concatenateFilePath(dirPath, name);
}

void FileUtils::removeFile(const QString &p_filePath)
{
    Q_ASSERT(QFileInfo(p_filePath).isFile());
    QFile file(p_filePath);
    if (!file.remove()) {
        Exception::throwOne(Exception::Type::FailToRemoveFile,
                            QString("failed to remove file: %1").arg(p_filePath));
    }
}

bool FileUtils::removeDirIfEmpty(const QString &p_dirPath)
{
    QDir dir(p_dirPath);
    if (!dir.isEmpty()) {
        return false;
    }

    if (!dir.rmdir(p_dirPath)) {
        Exception::throwOne(Exception::Type::FailToRemoveFile,
                            QString("failed to remove directory: %1").arg(p_dirPath));
        return false;
    }

    return true;
}

void FileUtils::removeDir(const QString &p_dirPath)
{
    QDir dir(p_dirPath);
    if (!dir.removeRecursively()) {
        Exception::throwOne(Exception::Type::FailToRemoveFile,
                            QString("failed to remove directory recursively: %1").arg(p_dirPath));
    }
}

bool FileUtils::isPlatformNameCaseSensitive()
{
#if defined(Q_OS_WIN)
    return false;
#else
    return true;
#endif
}

bool FileUtils::isText(const QString &p_filePath)
{
    QMimeDatabase mimeDatabase;
    auto mimeType = mimeDatabase.mimeTypeForFile(p_filePath);
    if (mimeType.name().startsWith(QStringLiteral("text/"))) {
        return true;
    }

    return mimeType.inherits(QStringLiteral("text/plain"));
}

QImage FileUtils::imageFromFile(const QString &p_filePath)
{
    QImage img(p_filePath);
    if (!img.isNull()) {
        return img;
    }

    // @p_filePath may has a wrong suffix which indicates a wrong image format.
    img.loadFromData(readFile(p_filePath));
    return img;
}

QPixmap FileUtils::pixmapFromFile(const QString &p_filePath)
{
    QPixmap pixmap;
    pixmap.loadFromData(readFile(p_filePath));
    return pixmap;
}

QString FileUtils::generateUniqueFileName(const QString &p_folderPath,
                                          const QString &p_hints,
                                          const QString &p_suffix)
{
    auto fileName = generateRandomFileName(p_hints, p_suffix);
    int suffixIdx = fileName.lastIndexOf(QLatin1Char('.'));
    auto baseName = suffixIdx == -1 ? fileName : fileName.left(suffixIdx);
    auto suffix = suffixIdx == -1 ? QStringLiteral("") : fileName.mid(suffixIdx);
    int index = 1;
    while (childExistsCaseInsensitive(p_folderPath, fileName)) {
        fileName = QString("%1_%2%3").arg(baseName, QString::number(index++), suffix);
    }

    return fileName;
}

QString FileUtils::generateRandomFileName(const QString &p_hints, const QString &p_suffix)
{
    Q_UNUSED(p_hints);

    const QString timeStamp(QDateTime::currentDateTime().toString(QStringLiteral("sszzzmmHHMMdd")));
    QString baseName = QString::number(timeStamp.toLongLong() + qrand());

    QString suffix;
    if (!p_suffix.isEmpty()) {
        suffix = QLatin1Char('.') + p_suffix.toLower();
    }

    return baseName + suffix;
}

QTemporaryFile *FileUtils::createTemporaryFile(const QString &p_suffix)
{
    QString xx = p_suffix.isEmpty() ? QStringLiteral("XXXXXX") : QStringLiteral("XXXXXX.");
    return new QTemporaryFile(QDir::tempPath() + QDir::separator() + xx + p_suffix);
}

QString FileUtils::generateFileNameWithSequence(const QString &p_folderPath,
                                                const QString &p_baseName,
                                                const QString &p_suffix)
{
    auto fileName = p_suffix.isEmpty() ? p_baseName : p_baseName + QLatin1Char('.') + p_suffix;
    auto suffix = p_suffix.isEmpty() ? QString() : QStringLiteral(".") + p_suffix;
    int index = 1;
    while (childExistsCaseInsensitive(p_folderPath, fileName)) {
        fileName = QString("%1_%2%3").arg(p_baseName, QString::number(index++), suffix);
    }

    return fileName;
}

void FileUtils::removeEmptyDir(const QString &p_dirPath)
{
    QDir dir(p_dirPath);
    qDebug() << "removeEmptyDir" << p_dirPath << dir.isEmpty();
    if (dir.isEmpty()) {
        return;
    }

    auto childDirs = dir.entryInfoList(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const auto &child : childDirs) {
        const auto childPath = child.absoluteFilePath();
        removeEmptyDir(childPath);
        removeDirIfEmpty(childPath);
    }
}
