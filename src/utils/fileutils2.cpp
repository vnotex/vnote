#include "fileutils2.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QRandomGenerator>
#include <QTemporaryFile>
#include <QTextStream>
#include <QThread>

#include "pathutils.h"

using namespace vnotex;

namespace {
// SSOT for the staging marker filename. The marker is written by
// generateCloneStagingDir, consumed by sweepOrphanStagingDirs, and
// disposed by renameStagingToFinal on success.
static const QString kStagingMarkerName = QStringLiteral("staging-marker.json");
} // namespace

Error FileUtils2::readFile(const QString &p_filePath, QByteArray *p_data) {
  Q_ASSERT(p_data);
  QFile file(p_filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return Error::error(ErrorCode::FailToReadFile,
                        QStringLiteral("failed to read file: %1").arg(p_filePath));
  }

  *p_data = file.readAll();
  return Error::ok();
}

Error FileUtils2::readTextFile(const QString &p_filePath, QString *p_text) {
  Q_ASSERT(p_text);
  QFile file(p_filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return Error::error(ErrorCode::FailToReadFile,
                        QStringLiteral("failed to read file: %1").arg(p_filePath));
  }

  // TODO: determine the encoding of the text.
  *p_text = QString(file.readAll());
  file.close();
  return Error::ok();
}

Error FileUtils2::readJsonFile(const QString &p_filePath, QJsonObject *p_json) {
  Q_ASSERT(p_json);
  QByteArray data;
  Error err = readFile(p_filePath, &data);
  if (err) {
    return err;
  }

  *p_json = QJsonDocument::fromJson(data).object();
  return Error::ok();
}

Error FileUtils2::writeFile(const QString &p_filePath, const QByteArray &p_data) {
  QFile file(p_filePath);
  if (!file.open(QIODevice::WriteOnly)) {
    return Error::error(ErrorCode::FailToWriteFile,
                        QStringLiteral("failed to write to file: %1").arg(p_filePath));
  }

  file.write(p_data);
  file.close();
  return Error::ok();
}

Error FileUtils2::writeFile(const QString &p_filePath, const QString &p_text) {
  QFile file(p_filePath);
  if (!file.open(QIODevice::WriteOnly)) {
    return Error::error(ErrorCode::FailToWriteFile,
                        QStringLiteral("failed to write to file: %1").arg(p_filePath));
  }

  QTextStream stream(&file);
  stream << p_text;
  file.close();
  return Error::ok();
}

Error FileUtils2::writeFile(const QString &p_filePath, const QJsonObject &p_jobj) {
  return writeFile(p_filePath, QJsonDocument(p_jobj).toJson());
}

Error FileUtils2::renameFile(const QString &p_path, const QString &p_name) {
  if (!PathUtils::isLegalFileName(p_name)) {
    return Error::error(ErrorCode::InvalidArgument,
                        QStringLiteral("illegal file name: %1").arg(p_name));
  }

  QString newFilePath(PathUtils::concatenateFilePath(PathUtils::parentDirPath(p_path), p_name));
  QFile file(p_path);
  if (!file.exists()) {
    return Error::error(ErrorCode::FileMissingOnDisk,
                        QStringLiteral("file does not exist: %1").arg(p_path));
  }

  if (!file.rename(newFilePath)) {
    return Error::error(ErrorCode::FailToRenameFile,
                        QStringLiteral("failed to rename file: %1").arg(p_path));
  }

  return Error::ok();
}

Error FileUtils2::copyFile(const QString &p_filePath, const QString &p_destPath, bool p_move) {
  if (PathUtils::areSamePaths(p_filePath, p_destPath)) {
    return Error::ok();
  }

  QDir dir;
  if (!dir.mkpath(PathUtils::parentDirPath(p_destPath))) {
    return Error::error(
        ErrorCode::FailToCreateDir,
        QStringLiteral("failed to create directory: %1").arg(PathUtils::parentDirPath(p_destPath)));
  }

  // Remove existing destination file to allow overwrite.
  if (QFileInfo::exists(p_destPath)) {
    // Clear the read-only attribute first; QFile::remove fails on read-only
    // files on Windows. Destinations dumped from a read-only .rcc resource are
    // themselves read-only, so this is required for upgrades to overwrite them.
    QFile::setPermissions(p_destPath,
                          QFile::permissions(p_destPath) | QFileDevice::WriteOwner |
                              QFileDevice::WriteUser);
    if (!QFile::remove(p_destPath)) {
      return Error::error(
          ErrorCode::FailToRemoveFile,
          QStringLiteral("failed to remove existing file for overwrite: %1").arg(p_destPath));
    }
  }

  bool success = false;
  if (p_move) {
    QFile file(p_filePath);
    success = file.rename(p_destPath);
  } else {
    success = QFile::copy(p_filePath, p_destPath);
  }

  if (!success) {
    return Error::error(ErrorCode::FailToCopyFile,
                        QStringLiteral("failed to copy file: %1 %2").arg(p_filePath, p_destPath));
  }

  if (!p_move) {
    // QFile::copy propagates the source's permissions to the destination. When
    // the source is a read-only .rcc resource, the new file is read-only, which
    // would break the next upgrade's overwrite. Make it writable.
    if (!QFile::setPermissions(p_destPath,
                               QFile::permissions(p_destPath) | QFileDevice::WriteOwner |
                                   QFileDevice::WriteUser)) {
      qWarning() << "failed to make copied file writable, subsequent overwrites may fail:"
                 << p_destPath;
    }
  }

  return Error::ok();
}

Error FileUtils2::copyDir(const QString &p_dirPath, const QString &p_destPath, bool p_move) {
  if (PathUtils::areSamePaths(p_dirPath, p_destPath)) {
    return Error::ok();
  }

  // QDir.rename() could not move directory across drives.

  // Create target directory (mkpath succeeds if it already exists).
  QDir destDir(p_destPath);
  if (!destDir.mkpath(p_destPath)) {
    return Error::error(ErrorCode::FailToCreateDir,
                        QStringLiteral("failed to create directory: %1").arg(p_destPath));
  }

  // Copy directory contents recursively (merge if target exists).
  QDir srcDir(p_dirPath);
  auto nodes = srcDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::Hidden | QDir::NoSymLinks |
                                    QDir::NoDotAndDotDot);
  for (const auto &node : nodes) {
    auto name = node.fileName();
    if (node.isDir()) {
      Error err = copyDir(srcDir.filePath(name), destDir.filePath(name), p_move);
      if (err) {
        return err;
      }
    } else {
      Q_ASSERT(node.isFile());
      Error err = copyFile(srcDir.filePath(name), destDir.filePath(name), p_move);
      if (err) {
        return err;
      }
    }
  }

  if (p_move) {
    if (!destDir.rmdir(p_dirPath)) {
      return Error::error(
          ErrorCode::FailToRemoveDir,
          QStringLiteral("failed to remove source directory after move: %1").arg(p_dirPath));
    }
  }

  return Error::ok();
}

Error FileUtils2::removeFile(const QString &p_filePath) {
  QFile file(p_filePath);
  if (!file.exists()) {
    // File doesn't exist, consider it already removed.
    return Error::ok();
  }

  if (!file.remove()) {
    return Error::error(ErrorCode::FailToRemoveFile,
                        QStringLiteral("failed to remove file: %1").arg(p_filePath));
  }

  return Error::ok();
}

Error FileUtils2::removeDirIfEmpty(const QString &p_dirPath, bool *p_removed) {
  Q_ASSERT(p_removed);
  *p_removed = false;

  QDir dir(p_dirPath);
  if (!dir.exists()) {
    // Directory doesn't exist, nothing to remove.
    return Error::ok();
  }

  if (!dir.isEmpty()) {
    // Directory is not empty, don't remove.
    return Error::ok();
  }

  if (!dir.rmdir(p_dirPath)) {
    return Error::error(ErrorCode::FailToRemoveDir,
                        QStringLiteral("failed to remove directory: %1").arg(p_dirPath));
  }

  *p_removed = true;
  return Error::ok();
}

Error FileUtils2::removeDir(const QString &p_dirPath) {
  QDir dir(p_dirPath);
  if (!dir.exists()) {
    // Directory doesn't exist, consider it already removed.
    return Error::ok();
  }

  if (!dir.removeRecursively()) {
    return Error::error(
        ErrorCode::FailToRemoveDir,
        QStringLiteral("failed to remove directory recursively: %1").arg(p_dirPath));
  }

  return Error::ok();
}

// --- Non-throwing methods (same implementation as FileUtils) ---

bool FileUtils2::childExistsCaseInsensitive(const QString &p_dirPath, const QString &p_name) {
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

bool FileUtils2::existsCaseInsensitive(const QString &p_path) {
  return childExistsCaseInsensitive(PathUtils::parentDirPath(p_path), PathUtils::fileName(p_path));
}

QString FileUtils2::renameIfExistsCaseInsensitive(const QString &p_path) {
  QFileInfo fi(p_path);
  auto dirPath = fi.absolutePath();
  auto baseName = fi.completeBaseName();
  auto suffix = fi.suffix();
  auto name = fi.fileName();
  int idx = 1;
  while (childExistsCaseInsensitive(dirPath, name)) {
    name = QStringLiteral("%1_%2").arg(baseName, QString::number(idx));
    if (!suffix.isEmpty()) {
      name += QStringLiteral(".") + suffix;
    }

    ++idx;
  }

  return PathUtils::concatenateFilePath(dirPath, name);
}

bool FileUtils2::isPlatformNameCaseSensitive() {
#if defined(Q_OS_WIN)
  return false;
#else
  return true;
#endif
}

bool FileUtils2::isText(const QString &p_filePath) {
  QMimeDatabase mimeDatabase;
  auto mimeType = mimeDatabase.mimeTypeForFile(p_filePath);
  const auto name = mimeType.name();
  if (name.startsWith(QStringLiteral("text/")) ||
      name == QStringLiteral("application/x-zerosize")) {
    return true;
  }

  return mimeType.inherits(QStringLiteral("text/plain"));
}

bool FileUtils2::isImage(const QString &p_filePath) {
  QMimeDatabase mimeDatabase;
  auto mimeType = mimeDatabase.mimeTypeForFile(p_filePath);
  if (mimeType.name().startsWith(QStringLiteral("image/"))) {
    return true;
  }
  return false;
}

QString FileUtils2::generateUniqueFileName(const QString &p_folderPath, const QString &p_hints,
                                           const QString &p_suffix) {
  auto fileName = generateRandomFileName(p_hints, p_suffix);
  int suffixIdx = fileName.lastIndexOf(QLatin1Char('.'));
  auto baseName = suffixIdx == -1 ? fileName : fileName.left(suffixIdx);
  auto suffix = suffixIdx == -1 ? QStringLiteral("") : fileName.mid(suffixIdx);
  int index = 1;
  while (childExistsCaseInsensitive(p_folderPath, fileName)) {
    fileName = QStringLiteral("%1_%2%3").arg(baseName, QString::number(index++), suffix);
  }

  return fileName;
}

QString FileUtils2::generateRandomFileName(const QString &p_hints, const QString &p_suffix) {
  Q_UNUSED(p_hints);

  const QString baseName(QString::number(QRandomGenerator::global()->bounded(0x10000), 16));

  QString suffix;
  if (!p_suffix.isEmpty()) {
    suffix = QLatin1Char('.') + p_suffix.toLower();
  }

  return baseName + suffix;
}

QTemporaryFile *FileUtils2::createTemporaryFile(const QString &p_suffix) {
  QString xx = p_suffix.isEmpty() ? QStringLiteral("XXXXXX") : QStringLiteral("XXXXXX.");
  return new QTemporaryFile(QDir::tempPath() + QDir::separator() + xx + p_suffix);
}

QString FileUtils2::generateFileNameWithSequence(const QString &p_folderPath,
                                                 const QString &p_baseName,
                                                 const QString &p_suffix) {
  auto fileName = p_suffix.isEmpty() ? p_baseName : p_baseName + QLatin1Char('.') + p_suffix;
  auto suffix = p_suffix.isEmpty() ? QString() : QStringLiteral(".") + p_suffix;
  int index = 1;
  while (childExistsCaseInsensitive(p_folderPath, fileName)) {
    fileName = QStringLiteral("%1_%2%3").arg(p_baseName, QString::number(index++), suffix);
  }

  return fileName;
}

void FileUtils2::removeEmptyDir(const QString &p_dirPath) {
  QDir dir(p_dirPath);
  if (dir.isEmpty()) {
    return;
  }

  auto childDirs = dir.entryInfoList(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
  for (const auto &child : childDirs) {
    const auto childPath = child.absoluteFilePath();
    removeEmptyDir(childPath);
    bool removed = false;
    removeDirIfEmpty(childPath, &removed);
  }
}

QStringList FileUtils2::entryListRecursively(const QString &p_dirPath,
                                             const QStringList &p_nameFilters,
                                             QDir::Filters p_filters) {
  QStringList entries;

  QDir dir(p_dirPath);
  if (!dir.exists()) {
    return entries;
  }

  const auto curEntries = dir.entryList(p_nameFilters, p_filters | QDir::NoDotAndDotDot);
  for (const auto &e : curEntries) {
    entries.append(PathUtils::concatenateFilePath(p_dirPath, e));
  }

  const auto subdirs = dir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot);
  for (const auto &subdir : subdirs) {
    const auto dirPath = PathUtils::concatenateFilePath(p_dirPath, subdir);
    entries.append(entryListRecursively(dirPath, p_nameFilters, p_filters));
  }

  return entries;
}

QString FileUtils2::generateCloneStagingDir(const QString &p_finalParentDir,
                                            const QString &p_finalLeafName, QString *p_errorOut) {
  if (p_errorOut) {
    *p_errorOut = QString();
  }

  // Ensure parent directory exists
  QDir parentDir(p_finalParentDir);
  if (!parentDir.exists()) {
    if (p_errorOut) {
      *p_errorOut = QStringLiteral("Parent directory does not exist: %1").arg(p_finalParentDir);
    }
    return QString();
  }

  // Generate unique staging dir name with timestamp
  qint64 timestampMs = QDateTime::currentMSecsSinceEpoch();
  QString stagingDirName = QStringLiteral(".%1.vnote-clone-pending-%2")
                               .arg(p_finalLeafName, QString::number(timestampMs));

  // Ensure uniqueness by adding collision counter if needed
  QString stagingDirPath = parentDir.filePath(stagingDirName);
  int collisionCounter = 0;
  while (QDir(stagingDirPath).exists()) {
    ++collisionCounter;
    stagingDirName =
        QStringLiteral(".%1.vnote-clone-pending-%2-%3")
            .arg(p_finalLeafName, QString::number(timestampMs), QString::number(collisionCounter));
    stagingDirPath = parentDir.filePath(stagingDirName);
  }

  // Create the staging directory
  if (!parentDir.mkpath(stagingDirName)) {
    if (p_errorOut) {
      *p_errorOut = QStringLiteral("Failed to create staging directory: %1").arg(stagingDirPath);
    }
    return QString();
  }

  // Create marker file with metadata
  QString finalDirPath = parentDir.filePath(p_finalLeafName);
  QJsonObject markerJson;
  markerJson.insert(QStringLiteral("createdUtc"), QJsonValue(static_cast<double>(timestampMs)));
  markerJson.insert(QStringLiteral("finalDir"), QJsonValue(QDir(finalDirPath).absolutePath()));

  QString markerFilePath = QDir(stagingDirPath).filePath(kStagingMarkerName);
  QFile markerFile(markerFilePath);
  if (!markerFile.open(QIODevice::WriteOnly)) {
    if (p_errorOut) {
      *p_errorOut = QStringLiteral("Failed to create marker file: %1").arg(markerFilePath);
    }
    QDir(stagingDirPath).removeRecursively();
    return QString();
  }

  markerFile.write(QJsonDocument(markerJson).toJson());
  markerFile.close();

  return QDir(stagingDirPath).absolutePath();
}

bool FileUtils2::renameStagingToFinal(const QString &p_stagingDir, const QString &p_finalDir,
                                      QString *p_errorOut) {
  if (p_errorOut) {
    *p_errorOut = QString();
  }

  QDir stagingDir(p_stagingDir);
  QDir finalDir(p_finalDir);

  // Check if staging dir exists
  if (!stagingDir.exists()) {
    if (p_errorOut) {
      *p_errorOut = QStringLiteral("Staging directory does not exist: %1").arg(p_stagingDir);
    }
    return false;
  }

  // Check if final dir already exists (collision)
  if (finalDir.exists()) {
    if (p_errorOut) {
      *p_errorOut = QStringLiteral("Final directory already exists: %1").arg(p_finalDir);
    }
    return false;
  }

  // Perform atomic rename using QDir::rename
  // QDir::rename is atomic on POSIX; on Windows it's best-effort.
  // The staging dir MUST be on the same filesystem as the final dir for atomicity.
  if (!stagingDir.rename(p_stagingDir, p_finalDir)) {
    if (p_errorOut) {
      *p_errorOut = QStringLiteral("Failed to rename staging directory to final destination")
                        .append(QStringLiteral(" (%1 -> %2)").arg(p_stagingDir, p_finalDir));
    }
    return false;
  }

  // Best-effort: the staging marker has no meaning in the final dir (sweep
  // matches by .*.vnote-clone-pending-* name, which the rename has already
  // changed). Without this delete, the file appears as an "external file"
  // in the notebook explorer because BundledFolderManager::ListExternalNodes
  // does not skip it. Failure here is cosmetic only — never fail the rename
  // on it (the controller would otherwise tear down the user's freshly
  // cloned notebook).
  const QString markerPath = QDir(p_finalDir).filePath(kStagingMarkerName);
  if (QFile::exists(markerPath) && !QFile::remove(markerPath)) {
    qWarning() << "renameStagingToFinal: failed to remove staging marker at" << markerPath;
  }

  return true;
}

bool FileUtils2::removeStagingDir(const QString &p_stagingDir, QString *p_errorOut) {
  if (p_errorOut) {
    *p_errorOut = QString();
  }

  QDir dir(p_stagingDir);

  // Reuse the Windows 20×100ms retry pattern from NewNotebookController
  // This handles the libgit2 file-handle race where the OS may hold transient
  // locks briefly after file operations complete.
  // See: src/controllers/newnotebookcontroller.cpp:268-278
  bool removed = false;
  for (int attempt = 0; attempt < 20 && dir.exists(); ++attempt) {
    removed = dir.removeRecursively();
    if (removed && !dir.exists()) {
      break;
    }
    QThread::msleep(100);
  }

  if (dir.exists()) {
    if (p_errorOut) {
      *p_errorOut = QStringLiteral("Failed to remove staging directory after 20 attempts: %1")
                        .arg(p_stagingDir);
    }
    return false;
  }

  return true;
}

QStringList FileUtils2::sweepOrphanStagingDirs(const QString &p_parentDir, qint64 p_olderThanMs) {
  QStringList orphans;

  QDir parentDir(p_parentDir);
  if (!parentDir.exists()) {
    return orphans;
  }

  qint64 now = QDateTime::currentMSecsSinceEpoch();

  // Find all entries matching pattern .*.vnote-clone-pending-*
  QStringList filters;
  filters << QStringLiteral(".*.vnote-clone-pending-*");

  // QDir::Hidden is REQUIRED: staging dir names start with '.', which Linux
  // (and Qt's default entryList behavior) treat as hidden. Without this flag
  // the sweep returns an empty list on Linux and silently no-ops on startup.
  auto entries = parentDir.entryList(filters, QDir::AllDirs | QDir::Hidden | QDir::NoDotAndDotDot |
                                                  QDir::NoSymLinks);

  for (const auto &entry : entries) {
    QString entryPath = parentDir.filePath(entry);

    // Read marker file to check creation timestamp
    QString markerPath = QDir(entryPath).filePath(kStagingMarkerName);
    QFile markerFile(markerPath);
    if (!markerFile.open(QIODevice::ReadOnly)) {
      continue; // Skip entries without valid marker
    }

    QJsonObject markerJson = QJsonDocument::fromJson(markerFile.readAll()).object();
    markerFile.close();

    qint64 createdUtc = markerJson.value(QStringLiteral("createdUtc")).toVariant().toLongLong();
    qint64 ageMs = now - createdUtc;

    // If older than threshold, add to orphan list
    if (ageMs > p_olderThanMs) {
      orphans.append(QDir(entryPath).absolutePath());
    }
  }

  return orphans;
}
