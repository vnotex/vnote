#ifndef FILEUTILS2_H
#define FILEUTILS2_H

#include <QByteArray>
#include <QDir>
#include <QJsonObject>
#include <QString>

#include <core/error.h>

class QTemporaryFile;

namespace vnotex {

// FileUtils2 provides file operations with error codes instead of exceptions.
// This is designed for gradual migration from FileUtils.
class FileUtils2 {
public:
  FileUtils2() = delete;

  // Read operations - return Error, output via pointer parameter.
  static Error readFile(const QString &p_filePath, QByteArray *p_data);

  static Error readTextFile(const QString &p_filePath, QString *p_text);

  static Error readJsonFile(const QString &p_filePath, QJsonObject *p_json);

  // Write operations - return Error.
  static Error writeFile(const QString &p_filePath, const QByteArray &p_data);

  static Error writeFile(const QString &p_filePath, const QString &p_text);

  static Error writeFile(const QString &p_filePath, const QJsonObject &p_jobj);

  // Rename file or dir.
  static Error renameFile(const QString &p_path, const QString &p_name);

  // Copy file. If p_move is true, move instead of copy.
  // Overwrites destination file if it exists.
  static Error copyFile(const QString &p_filePath, const QString &p_destPath, bool p_move = false);

  // Copy directory recursively. If p_move is true, move instead of copy.
  // Merges if target directory exists, overwriting files with same names.
  static Error copyDir(const QString &p_dirPath, const QString &p_destPath, bool p_move = false);

  // Remove file.
  static Error removeFile(const QString &p_filePath);

  // Remove directory if empty.
  // @p_removed: output parameter, true if directory was removed.
  static Error removeDirIfEmpty(const QString &p_dirPath, bool *p_removed);

  // Remove directory recursively.
  static Error removeDir(const QString &p_dirPath);

  // --- Non-throwing methods (same as FileUtils) ---

  static bool childExistsCaseInsensitive(const QString &p_dirPath, const QString &p_name);

  static bool existsCaseInsensitive(const QString &p_path);

  static QString renameIfExistsCaseInsensitive(const QString &p_path);

  static bool isPlatformNameCaseSensitive();

  static bool isText(const QString &p_filePath);

  static bool isImage(const QString &p_filePath);

  static QString generateUniqueFileName(const QString &p_folderPath, const QString &p_hints,
                                        const QString &p_suffix);

  static QString generateRandomFileName(const QString &p_hints, const QString &p_suffix);

  static QString generateFileNameWithSequence(const QString &p_folderPath,
                                              const QString &p_baseName,
                                              const QString &p_suffix = QString());

  static QTemporaryFile *createTemporaryFile(const QString &p_suffix);

  // Go through @p_dirPath recursively and delete all empty dirs.
  // @p_dirPath itself is not deleted.
  static void removeEmptyDir(const QString &p_dirPath);

  // Go through @p_dirPath recursively and get all entries.
  // @p_nameFilters is for each dir, not for all.
  static QStringList entryListRecursively(const QString &p_dirPath,
                                          const QStringList &p_nameFilters,
                                          QDir::Filters p_filters = QDir::NoFilter);

  // --- Staging directory helpers for safe clone/move operations ---

  // Create a unique staging directory under p_finalParentDir with the pattern:
  // .<p_finalLeafName>.vnote-clone-pending-<timestampMs>
  // Writes a marker file "staging-marker.json" inside containing:
  // {"createdUtc": <ms>, "finalDir": "<absolute path of intended final destination>"}
  // Returns absolute path of the new staging directory on success.
  // On failure, returns empty string and sets *p_errorOut.
  static QString generateCloneStagingDir(const QString &p_finalParentDir,
                                         const QString &p_finalLeafName, QString *p_errorOut);

  // Atomically rename staging directory to final destination.
  // Uses QDir::rename (best-effort atomic on POSIX, best-effort on Windows).
  // IMPORTANT: p_stagingDir MUST be on the same filesystem as p_finalDir for atomicity.
  // On success, also best-effort removes the staging marker file (it has no
  // meaning in the final dir; a failed removal logs a warning but still
  // returns true since the marker is cosmetic-only at that point).
  // Returns true on success; returns false + *p_errorOut on failure.
  // On failure, does NOT modify either directory.
  static bool renameStagingToFinal(const QString &p_stagingDir, const QString &p_finalDir,
                                   QString *p_errorOut);

  // Recursively remove a staging directory.
  // Reuses the Windows 20x100ms retry pattern to handle libgit2 file-handle races.
  // Returns true on success; returns false + *p_errorOut on failure.
  static bool removeStagingDir(const QString &p_stagingDir, QString *p_errorOut);

  // Scan p_parentDir for orphan staging directories matching pattern
  // .*.vnote-clone-pending-* whose marker file indicates age > p_olderThanMs.
  // Returns list of absolute paths that WOULD be swept (does NOT delete).
  // Caller decides whether to invoke removeStagingDir on each result.
  static QStringList sweepOrphanStagingDirs(const QString &p_parentDir, qint64 p_olderThanMs);
};
} // namespace vnotex

#endif // FILEUTILS2_H
