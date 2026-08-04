#ifndef FILEUTILS2_H
#define FILEUTILS2_H

#include <QByteArray>
#include <QDir>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

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

  // Recursively copy p_dirPath into p_destPath, continuing past per-node
  // failures instead of aborting on the first one (which copyDir does).
  // Every failing SOURCE path is appended to p_failedPaths (may be null); a
  // subdirectory whose creation failed is recorded as a single entry and its
  // siblings are still copied.
  // Destination-root-relative paths in p_skipExistingRelPaths (forward slashes,
  // compared case-insensitively) are NOT copied when they already exist as
  // regular files at the destination; such a skip is NOT a failure. An
  // incompatible destination node is handled as a normal copy failure.
  // Returns the FIRST error encountered, or Error::ok() when everything copied.
  // Copy-only: there is deliberately no p_move counterpart.
  //
  // copyDir() is intentionally left untouched: Exporter (attachment copy) and
  // FirstRunController (bundled notebook) both want fail-fast, and a silent
  // semantic change there would let a half-copied tree report success.
  static Error copyDirCollectingErrors(const QString &p_dirPath, const QString &p_destPath,
                                       QStringList *p_failedPaths = nullptr,
                                       const QSet<QString> *p_skipExistingRelPaths = nullptr);

  // Name of the per-folder version stamp file written inside the destination
  // directory by installVersionedDir(). Never present in the source tree, so
  // the copy itself can never overwrite it.
  static const char *const c_versionStampFileName;

  // Install a bundled directory into p_destDir, remembering p_version in a
  // stamp file so a COMPLETED install is skipped on the next call and a PARTIAL
  // one is retried.
  //
  // Order of operations (load-bearing):
  //   1. Validate p_srcDir exists and is a directory; otherwise return an error
  //      WITHOUT touching the destination or the stamp. This is what stops a
  //      missing resource from being recorded as a successful empty copy, and
  //      it also means a previously completed install is left intact (see the
  //      invariant below).
  //   2. When !p_force and the stamp's trimmed contents equal p_version,
  //      return Error::ok() without copying.
  //   3. Otherwise DELETE any existing stamp first; a deletion failure is a
  //      hard error returned before any copying, so a stale-but-plausible stamp
  //      can never survive a crash (or a failed forced copy) mid-install.
  //   4. copyDirCollectingErrors(...).
  //   5. On any failure, return that error and write NO stamp.
  //   6. On success, write the stamp durably (QSaveFile + checked write +
  //      commit). A stamp-write failure is returned as an error so the folder
  //      is retried on the next launch.
  //
  // INVARIANT: once step 3 has been reached, no stamp holding p_version can
  // exist in p_destDir unless the whole install succeeded. The one failure that
  // happens BEFORE step 3 -- an invalid source -- deliberately leaves an
  // existing installation (and its stamp) untouched, because there is nothing
  // to install and destroying a good install would be strictly worse.
  //
  // p_failedPaths collects the failing SOURCE paths of step 4, plus the
  // destination stamp path when the failure is the stamp removal/write itself
  // (that path is the useful diagnostic in those cases).
  static Error installVersionedDir(const QString &p_srcDir, const QString &p_destDir,
                                   const QString &p_version,
                                   QStringList *p_failedPaths = nullptr, bool p_force = false,
                                   const QSet<QString> *p_skipExistingRelPaths = nullptr);

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
