#include "updateinstaller.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStorageInfo>
#include <QThread>
#include <QVarLengthArray>

#include <algorithm>

#include "updatemanifest.h"

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

using namespace vnotex;

constexpr int UpdateInstaller::PendingPlan::c_schema;

// ===========================================================================
// Local state, constants and helpers
// ===========================================================================
namespace {

const QString c_stagingDirName = QStringLiteral(".vnote-update");
const QString c_backupDirName = QStringLiteral(".vnote-old");
const QString c_downloadDirName = QStringLiteral("download");
const QString c_stagedDirName = QStringLiteral("staged");
const QString c_pendingFileName = QStringLiteral("pending.json");
const QString c_journalFileName = QStringLiteral("journal.json");
const QString c_resultFileName = QStringLiteral("result.json");
const QString c_recoveryFileName = QStringLiteral("RECOVERY.txt");
const QString c_writeTestFileName = QStringLiteral(".vnote-write-test");

// --- Test seams ------------------------------------------------------------
UpdateInstaller::FaultPoint g_faultPoint = UpdateInstaller::FaultPoint::None;
int g_faultOpIndex = -1;
QString g_forcedRenameFailurePath;
bool g_forceAtomicRenameUnsupported = false;
bool g_retryBackoffEnabled = true;

bool faultFires(UpdateInstaller::FaultPoint p_point, int p_opIndex) {
  if (g_faultPoint != p_point) {
    return false;
  }
  return g_faultOpIndex < 0 || g_faultOpIndex == p_opIndex;
}

// --- Small filesystem helpers ---------------------------------------------

QString joinPath(const QString &p_base, const QString &p_relative) {
  if (p_relative.isEmpty()) {
    return QDir::cleanPath(p_base);
  }
  return QDir::cleanPath(p_base + QLatin1Char('/') + p_relative);
}

bool pathExists(const QString &p_path) {
  // QFileInfo::exists() is false for a dangling symlink; that is what we want,
  // since such a path cannot be renamed meaningfully either.
  return QFileInfo::exists(p_path);
}

bool isDirectory(const QString &p_path) { return QFileInfo(p_path).isDir(); }

bool ensureParentDir(const QString &p_path) {
  const QString parent = QFileInfo(p_path).absolutePath();
  if (parent.isEmpty()) {
    return false;
  }
  return QDir().mkpath(parent);
}

// The list of directories (install-root-relative, outermost first) that must be
// created for p_relativePath to be writable. Computed BEFORE any mkpath so the
// journal can record exactly what rollback has to remove.
QStringList missingParentDirs(const QString &p_installDir, const QString &p_relativePath) {
  QStringList missing;
  const QStringList parts = p_relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  QString acc;
  // Everything except the final component.
  for (int i = 0; i + 1 < parts.size(); ++i) {
    if (!acc.isEmpty()) {
      acc += QLatin1Char('/');
    }
    acc += parts.at(i);
    if (!pathExists(joinPath(p_installDir, acc))) {
      missing.append(acc);
    }
  }
  return missing;
}

bool removeEmptyDir(const QString &p_path) {
  QDir dir(p_path);
  if (!dir.exists()) {
    return true;
  }
  if (!dir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
    return false;
  }
  return QDir().rmdir(p_path);
}

// Rename honoring the injected-failure seam. Used for every ORDINARY operation;
// the executable never goes through here.
bool renamePath(const QString &p_from, const QString &p_to, const QString &p_relativeDest) {
  if (!g_forcedRenameFailurePath.isEmpty() &&
      UpdateManifest::pathKey(p_relativeDest) ==
          UpdateManifest::pathKey(g_forcedRenameFailurePath)) {
    return false;
  }

  if (!ensureParentDir(p_to)) {
    return false;
  }

  // QFile::rename refuses to overwrite; the caller always moves the existing
  // destination out of the way first, but be explicit about it here so a bug
  // surfaces as a failed op instead of silent data loss.
  if (pathExists(p_to)) {
    return false;
  }

  if (isDirectory(p_from)) {
    return QDir().rename(p_from, p_to);
  }
  return QFile::rename(p_from, p_to);
}

bool removeRecursively(const QString &p_path) {
  if (!pathExists(p_path)) {
    return true;
  }
  if (isDirectory(p_path)) {
    return QDir(p_path).removeRecursively();
  }
  return QFile::remove(p_path);
}

bool writeJsonAtomically(const QString &p_path, const QJsonObject &p_obj) {
  if (!ensureParentDir(p_path)) {
    return false;
  }
  QSaveFile file(p_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const QByteArray bytes = QJsonDocument(p_obj).toJson(QJsonDocument::Indented);
  if (file.write(bytes) != bytes.size()) {
    file.cancelWriting();
    return false;
  }
  return file.commit();
}

bool readJson(const QString &p_path, QJsonObject *p_out) {
  QFile file(p_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  const QByteArray bytes = file.readAll();
  file.close();

  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    return false;
  }
  *p_out = doc.object();
  return true;
}

template <typename Fn> bool withRetry(Fn &&p_fn) {
  static const int delays[] = {200, 500, 1200};
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (p_fn()) {
      return true;
    }
    if (attempt < 2 && g_retryBackoffEnabled) {
      QThread::msleep(static_cast<unsigned long>(delays[attempt]));
    }
  }
  return false;
}

// ===========================================================================
// Executable-safe rename
// ===========================================================================
//
// EMPIRICAL FINDING (2026-07-30, Windows 11 24H2 / NTFS, verified by a
// standalone probe against a real PE):
//
//   target state              | FileRenameInfoEx  | FileRenameInfoEx | MoveFileExW
//                             | POSIX|REPLACE     | REPLACE only     | (aside)
//   --------------------------+-------------------+------------------+------------
//   no open handle            | OK                | OK               | OK
//   open handle, no section   | OK                | ACCESS_DENIED    | OK
//   data section mapped       | OK                | ACCESS_DENIED    | OK
//   IMAGE section mapped      | ACCESS_DENIED     | ACCESS_DENIED    | OK
//
// The last row is decisive: a running executable is mapped as an IMAGE section,
// and Windows refuses to unlink the name of such a file no matter which rename
// flavor is used. Renaming a mapped image ASIDE, however, is allowed -- that is
// the classic running-executable trick and it works with every flavor.
//
// Therefore the executable swap CANNOT be a single namespace operation. It is
// two renames:
//
//   1. canonical  -> <backupDir>/<path>     (allowed on a mapped image)
//   2. staged     -> canonical              (destination no longer exists)
//
// Between them the canonical name does not exist. That window is one syscall
// wide, is journaled (state BackedUp), is recoverable from either side by
// hashing, and RECOVERY.txt documents the manual fix. See the "ReplaceExecutable"
// section of the plan and the root AGENTS.md for the full rationale.
//
// POSIX semantics is still preferred for BOTH renames: per the table above it is
// strictly more permissive than plain REPLACE whenever any handle is open on the
// destination, which is the normal situation during an update.

#ifdef Q_OS_WIN

// ntifs.h names; redefined locally so the build does not depend on which SDK
// headers happen to be reachable.
constexpr DWORD kFileRenameReplaceIfExists = 0x00000001;
constexpr DWORD kFileRenamePosixSemantics = 0x00000002;
// FILE_INFO_BY_HANDLE_CLASS::FileRenameInfoEx
constexpr int kFileRenameInfoEx = 22;

// True when the error means "this host does not implement FileRenameInfoEx",
// as opposed to "the operation was refused". Windows 7 / 8.1 -- still a
// supported VNote variant via the Qt5 `-windows7` build -- predate the info
// class entirely, so falling back there is correct. An ACCESS_DENIED, by
// contrast, is a real refusal and must NOT be papered over.
bool isUnsupportedInfoClassError(DWORD p_error) {
  switch (p_error) {
  case ERROR_INVALID_PARAMETER:
  case ERROR_NOT_SUPPORTED:
  case ERROR_INVALID_FUNCTION:
  case ERROR_CALL_NOT_IMPLEMENTED:
    return true;
  default:
    return false;
  }
}

bool renameWithPosixSemantics(const QString &p_source, const QString &p_dest, DWORD *p_lastError) {
  const QString nativeSource = QDir::toNativeSeparators(p_source);
  const QString nativeDest = QDir::toNativeSeparators(QFileInfo(p_dest).absoluteFilePath());

  // The SOURCE handle needs DELETE access for a rename.
  HANDLE handle = ::CreateFileW(reinterpret_cast<const wchar_t *>(nativeSource.utf16()),
                                DELETE | SYNCHRONIZE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    if (p_lastError) {
      *p_lastError = ::GetLastError();
    }
    return false;
  }

  const int nameChars = nativeDest.size();
  const size_t bufSize =
      sizeof(FILE_RENAME_INFO) + static_cast<size_t>(nameChars + 1) * sizeof(wchar_t);
  QByteArray buffer(static_cast<int>(bufSize), '\0');
  auto *info = reinterpret_cast<FILE_RENAME_INFO *>(buffer.data());

  // NOTE: this is the Flags member of the union, NOT ReplaceIfExists. Writing
  // the BOOLEAN member would silently drop POSIX semantics.
  info->Flags = kFileRenamePosixSemantics | kFileRenameReplaceIfExists;
  info->RootDirectory = nullptr;
  info->FileNameLength = static_cast<DWORD>(nameChars * sizeof(wchar_t));
  memcpy(info->FileName, nativeDest.utf16(), static_cast<size_t>(nameChars) * sizeof(wchar_t));

  const BOOL ok = ::SetFileInformationByHandle(
      handle, static_cast<FILE_INFO_BY_HANDLE_CLASS>(kFileRenameInfoEx), info,
      static_cast<DWORD>(bufSize));
  const DWORD err = ok ? ERROR_SUCCESS : ::GetLastError();
  ::CloseHandle(handle);

  if (p_lastError) {
    *p_lastError = err;
  }
  return ok != FALSE;
}

bool renameWin(const QString &p_source, const QString &p_dest, DWORD *p_lastError) {
  DWORD err = ERROR_SUCCESS;
  if (renameWithPosixSemantics(p_source, p_dest, &err)) {
    return true;
  }

  if (isUnsupportedInfoClassError(err)) {
    const QString nativeSource = QDir::toNativeSeparators(p_source);
    const QString nativeDest = QDir::toNativeSeparators(QFileInfo(p_dest).absoluteFilePath());
    if (::MoveFileExW(reinterpret_cast<const wchar_t *>(nativeSource.utf16()),
                      reinterpret_cast<const wchar_t *>(nativeDest.utf16()),
                      MOVEFILE_REPLACE_EXISTING)) {
      return true;
    }
    err = ::GetLastError();
  }

  if (p_lastError) {
    *p_lastError = err;
  }
  return false;
}

#endif // Q_OS_WIN

// Cross-platform front end for the two renames the executable swap is allowed
// to use. Never used for ordinary files -- those go through renamePath().
bool executableRename(const QString &p_source, const QString &p_dest, QString *p_error) {
#ifdef Q_OS_WIN
  DWORD err = ERROR_SUCCESS;
  if (renameWin(p_source, p_dest, &err)) {
    return true;
  }
  if (p_error) {
    *p_error = QStringLiteral("rename '%1' -> '%2' failed (win32 error %3)")
                   .arg(p_source, p_dest)
                   .arg(static_cast<quint32>(err));
  }
  return false;
#else
  // POSIX rename(2) is atomic and replaces the destination.
  if (::rename(QFile::encodeName(p_source).constData(),
               QFile::encodeName(p_dest).constData()) == 0) {
    return true;
  }
  if (p_error) {
    *p_error = QStringLiteral("rename '%1' -> '%2' failed").arg(p_source, p_dest);
  }
  return false;
#endif
}

// ===========================================================================
// Journal
// ===========================================================================

struct JournalOp {
  enum class Type {
    // Back up and remove a tree whose on-disk type conflicts with what the
    // target needs (directory where a file goes, or vice versa).
    ConflictRemove,
    // Back up and remove a file the target no longer ships. BLOCKING: a failed
    // deletion rolls the transaction back, because a stale DLL that survived
    // would also vanish from the new manifest and could never be cleaned up.
    Delete,
    // New file: no backup, staged -> target.
    Add,
    // Existing file: backup, then staged -> target.
    Replace,
    // The canonical executable. NEVER goes through the generic backup-then-move
    // or the generic reverse rollback.
    ReplaceExecutable,
  };

  enum class State {
    // Recorded, nothing done yet.
    Intent,
    // Backup taken (or, for Add, parent directories created): the pre-move
    // checkpoint has been reached durably.
    BackedUp,
    // Fully applied.
    Done,
    // Undone by rollback.
    Reverted,
  };

  Type type = Type::Replace;
  State state = State::Intent;
  QString path;
  QStringList createdDirs;

  // ReplaceExecutable only. expectedOldHash is the hash of the ACTUAL canonical
  // executable computed under the lease at apply time -- NOT the base-manifest
  // hash, which does not exist on a first managed update from a pre-manifest
  // install and is known to differ on the drift-triggered full-ZIP fallback.
  QString expectedOldHash;
  QString expectedNewHash;
};

struct Journal {
  enum class Phase {
    Applying,
    RollingBack,
    // Terminal states.
    Committed,
    RolledBack,
    ManualRecovery,
  };

  static constexpr int c_schema = 1;

  Phase phase = Phase::Applying;
  QString backupDir; // install-root-relative
  QString targetVersion;
  QString executablePath;
  QJsonObject targetManifest;
  QVector<JournalOp> ops;

  bool isTerminal() const {
    return phase == Phase::Committed || phase == Phase::RolledBack ||
           phase == Phase::ManualRecovery;
  }
};

constexpr int Journal::c_schema;

QString typeToString(JournalOp::Type p_type) {
  switch (p_type) {
  case JournalOp::Type::ConflictRemove:
    return QStringLiteral("conflictRemove");
  case JournalOp::Type::Delete:
    return QStringLiteral("delete");
  case JournalOp::Type::Add:
    return QStringLiteral("add");
  case JournalOp::Type::Replace:
    return QStringLiteral("replace");
  case JournalOp::Type::ReplaceExecutable:
    return QStringLiteral("replaceExecutable");
  }
  return QStringLiteral("replace");
}

bool typeFromString(const QString &p_value, JournalOp::Type *p_out) {
  if (p_value == QLatin1String("conflictRemove")) {
    *p_out = JournalOp::Type::ConflictRemove;
  } else if (p_value == QLatin1String("delete")) {
    *p_out = JournalOp::Type::Delete;
  } else if (p_value == QLatin1String("add")) {
    *p_out = JournalOp::Type::Add;
  } else if (p_value == QLatin1String("replace")) {
    *p_out = JournalOp::Type::Replace;
  } else if (p_value == QLatin1String("replaceExecutable")) {
    *p_out = JournalOp::Type::ReplaceExecutable;
  } else {
    return false;
  }
  return true;
}

QString stateToString(JournalOp::State p_state) {
  switch (p_state) {
  case JournalOp::State::Intent:
    return QStringLiteral("intent");
  case JournalOp::State::BackedUp:
    return QStringLiteral("backedUp");
  case JournalOp::State::Done:
    return QStringLiteral("done");
  case JournalOp::State::Reverted:
    return QStringLiteral("reverted");
  }
  return QStringLiteral("intent");
}

bool stateFromString(const QString &p_value, JournalOp::State *p_out) {
  if (p_value == QLatin1String("intent")) {
    *p_out = JournalOp::State::Intent;
  } else if (p_value == QLatin1String("backedUp")) {
    *p_out = JournalOp::State::BackedUp;
  } else if (p_value == QLatin1String("done")) {
    *p_out = JournalOp::State::Done;
  } else if (p_value == QLatin1String("reverted")) {
    *p_out = JournalOp::State::Reverted;
  } else {
    return false;
  }
  return true;
}

QString phaseToString(Journal::Phase p_phase) {
  switch (p_phase) {
  case Journal::Phase::Applying:
    return QStringLiteral("APPLYING");
  case Journal::Phase::RollingBack:
    return QStringLiteral("ROLLING_BACK");
  case Journal::Phase::Committed:
    return QStringLiteral("COMMITTED");
  case Journal::Phase::RolledBack:
    return QStringLiteral("ROLLED_BACK");
  case Journal::Phase::ManualRecovery:
    return QStringLiteral("MANUAL_RECOVERY");
  }
  return QStringLiteral("APPLYING");
}

bool phaseFromString(const QString &p_value, Journal::Phase *p_out) {
  if (p_value == QLatin1String("APPLYING")) {
    *p_out = Journal::Phase::Applying;
  } else if (p_value == QLatin1String("ROLLING_BACK")) {
    *p_out = Journal::Phase::RollingBack;
  } else if (p_value == QLatin1String("COMMITTED")) {
    *p_out = Journal::Phase::Committed;
  } else if (p_value == QLatin1String("ROLLED_BACK")) {
    *p_out = Journal::Phase::RolledBack;
  } else if (p_value == QLatin1String("MANUAL_RECOVERY")) {
    *p_out = Journal::Phase::ManualRecovery;
  } else {
    return false;
  }
  return true;
}

QJsonObject journalToJson(const Journal &p_journal) {
  QJsonObject obj;
  obj[QStringLiteral("schema")] = Journal::c_schema;
  obj[QStringLiteral("phase")] = phaseToString(p_journal.phase);
  obj[QStringLiteral("backupDir")] = p_journal.backupDir;
  obj[QStringLiteral("targetVersion")] = p_journal.targetVersion;
  obj[QStringLiteral("executablePath")] = p_journal.executablePath;
  obj[QStringLiteral("targetManifest")] = p_journal.targetManifest;

  QJsonArray ops;
  for (const JournalOp &op : p_journal.ops) {
    QJsonObject o;
    o[QStringLiteral("type")] = typeToString(op.type);
    o[QStringLiteral("state")] = stateToString(op.state);
    o[QStringLiteral("path")] = op.path;
    QJsonArray dirs;
    for (const QString &d : op.createdDirs) {
      dirs.append(d);
    }
    o[QStringLiteral("createdDirs")] = dirs;
    if (op.type == JournalOp::Type::ReplaceExecutable) {
      o[QStringLiteral("expectedOldHash")] = op.expectedOldHash;
      o[QStringLiteral("expectedNewHash")] = op.expectedNewHash;
    }
    ops.append(o);
  }
  obj[QStringLiteral("ops")] = ops;
  return obj;
}

bool journalFromJson(const QJsonObject &p_obj, Journal *p_out) {
  if (p_obj.value(QStringLiteral("schema")).toInt() != Journal::c_schema) {
    return false;
  }
  Journal j;
  if (!phaseFromString(p_obj.value(QStringLiteral("phase")).toString(), &j.phase)) {
    return false;
  }
  j.backupDir = p_obj.value(QStringLiteral("backupDir")).toString();
  j.targetVersion = p_obj.value(QStringLiteral("targetVersion")).toString();
  j.executablePath = p_obj.value(QStringLiteral("executablePath")).toString();
  j.targetManifest = p_obj.value(QStringLiteral("targetManifest")).toObject();

  const QJsonValue opsVal = p_obj.value(QStringLiteral("ops"));
  if (!opsVal.isArray()) {
    return false;
  }
  for (const QJsonValue &v : opsVal.toArray()) {
    if (!v.isObject()) {
      return false;
    }
    const QJsonObject o = v.toObject();
    JournalOp op;
    if (!typeFromString(o.value(QStringLiteral("type")).toString(), &op.type)) {
      return false;
    }
    if (!stateFromString(o.value(QStringLiteral("state")).toString(), &op.state)) {
      return false;
    }
    op.path = UpdateManifest::normalizePath(o.value(QStringLiteral("path")).toString());
    if (op.path.isEmpty()) {
      return false;
    }
    for (const QJsonValue &d : o.value(QStringLiteral("createdDirs")).toArray()) {
      const QString dir = UpdateManifest::normalizePath(d.toString());
      if (dir.isEmpty()) {
        return false;
      }
      op.createdDirs.append(dir);
    }
    op.expectedOldHash = o.value(QStringLiteral("expectedOldHash")).toString();
    op.expectedNewHash = o.value(QStringLiteral("expectedNewHash")).toString();
    j.ops.append(op);
  }

  *p_out = j;
  return true;
}

bool saveJournal(const QString &p_installDir, const Journal &p_journal) {
  return writeJsonAtomically(UpdateInstaller::journalPath(p_installDir),
                             journalToJson(p_journal));
}

bool loadJournal(const QString &p_installDir, Journal *p_out) {
  QJsonObject obj;
  if (!readJson(UpdateInstaller::journalPath(p_installDir), &obj)) {
    return false;
  }
  return journalFromJson(obj, p_out);
}

QStringList sortedUniquePaths(QStringList p_paths) {
  std::sort(p_paths.begin(), p_paths.end());
  p_paths.erase(std::unique(p_paths.begin(), p_paths.end()), p_paths.end());
  return p_paths;
}

// Paths the user must delete BEFORE overlaying the backup.
//
// This is not just the files the update ADDS. A path-type transition is applied
// as a ConflictRemove + Add pair (a directory becoming a file, or a file
// becoming a directory), and for those the backup holds an entry of the OTHER
// TYPE than what is now on disk. Copying a directory over a file - or a file
// over a directory - either fails outright or, if the tool forces it, the
// "delete the added paths" step afterwards would destroy the just-restored
// original. So every addition AND every conflict root is removed FIRST, and the
// overlay then lands on a clean tree.
QStringList pathsToRemoveBeforeRestore(const QVector<JournalOp> &p_ops) {
  QStringList paths;
  for (const JournalOp &op : p_ops) {
    if (op.type == JournalOp::Type::Add || op.type == JournalOp::Type::ConflictRemove) {
      paths.append(op.path);
    }
  }
  paths = sortedUniquePaths(paths);

  // Drop anything nested under another listed path: deleting the ancestor
  // recursively already covers it, and listing both invites confusion.
  QStringList pruned;
  for (const QString &path : paths) {
    bool covered = false;
    for (const QString &candidate : pruned) {
      if (UpdateManifest::pathKey(path).startsWith(UpdateManifest::pathKey(candidate) +
                                                   QLatin1Char('/'))) {
        covered = true;
        break;
      }
    }
    if (!covered) {
      pruned.append(path);
    }
  }
  return pruned;
}

QString recoveryText(const QString &p_installDir, const QString &p_targetVersion,
                     const QString &p_backupRelDir, const QStringList &p_pathsToRemoveFirst) {
  QString removalSection;
  if (p_pathsToRemoveFirst.isEmpty()) {
    removalSection = QStringLiteral(
        "  2. This update added no new files, so there is nothing to delete first.\n");
  } else {
    removalSection = QStringLiteral(
        "  2. Delete these paths from the install directory FIRST (delete\n"
        "     directories and everything inside them). This update created them,\n"
        "     and some of them replaced an entry of a different kind, so the\n"
        "     backup cannot simply be copied over the top of them:\n");
    for (const QString &path : p_pathsToRemoveFirst) {
      removalSection += QStringLiteral("       %1\n").arg(QDir::toNativeSeparators(path));
    }
  }

  return QStringLiteral(
             "VNote update recovery information\n"
             "=================================\n\n"
             "An update to version %1 was being applied to:\n"
             "  %2\n\n"
             "This directory holds the ORIGINAL files that were replaced or removed,\n"
             "laid out exactly like the install directory.\n\n"
             "If VNote no longer starts, restore the previous build by hand, IN THIS\n"
             "ORDER:\n"
             "  1. Close every VNote process (including QtWebEngineProcess.exe).\n"
             "%3"
             "  3. Copy every file from\n"
             "       %4\n"
             "     back over the install directory, overwriting what is there.\n"
             "     Do NOT copy RECOVERY.txt or journal.json themselves.\n"
             "  4. Delete the '.vnote-update' directory in the install directory.\n"
             "  5. Delete this backup directory.\n\n"
             "If any of that is unclear or does not work, the simplest and safest\n"
             "option is to download version %5 again and unpack it over a fresh\n"
             "folder:\n"
             "  https://github.com/vnotex/vnote/releases\n"
             "Your notes and settings are stored outside the install directory and\n"
             "are not affected by any of this.\n\n"
             "The write-ahead journal describing exactly what was done is at:\n"
             "  %6\n"
             "and a copy is kept in this directory once the update reaches a\n"
             "terminal state.\n\n"
             "Created at %7.\n")
      .arg(p_targetVersion, QDir::toNativeSeparators(p_installDir), removalSection,
           QDir::toNativeSeparators(joinPath(p_installDir, p_backupRelDir)), p_targetVersion,
           QDir::toNativeSeparators(UpdateInstaller::journalPath(p_installDir)),
           QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
}

QString outcomeToString(UpdateInstaller::ResultOutcome p_outcome) {
  switch (p_outcome) {
  case UpdateInstaller::ResultOutcome::None:
    return QStringLiteral("none");
  case UpdateInstaller::ResultOutcome::Applied:
    return QStringLiteral("applied");
  case UpdateInstaller::ResultOutcome::Retryable:
    return QStringLiteral("retryable");
  case UpdateInstaller::ResultOutcome::Failed:
    return QStringLiteral("failed");
  case UpdateInstaller::ResultOutcome::ManualRecovery:
    return QStringLiteral("manualRecovery");
  case UpdateInstaller::ResultOutcome::SpawnFailed:
    return QStringLiteral("spawnFailed");
  }
  return QStringLiteral("none");
}

UpdateInstaller::ResultOutcome outcomeFromString(const QString &p_value) {
  if (p_value == QLatin1String("applied")) {
    return UpdateInstaller::ResultOutcome::Applied;
  }
  if (p_value == QLatin1String("retryable")) {
    return UpdateInstaller::ResultOutcome::Retryable;
  }
  if (p_value == QLatin1String("failed")) {
    return UpdateInstaller::ResultOutcome::Failed;
  }
  if (p_value == QLatin1String("manualRecovery")) {
    return UpdateInstaller::ResultOutcome::ManualRecovery;
  }
  if (p_value == QLatin1String("spawnFailed")) {
    return UpdateInstaller::ResultOutcome::SpawnFailed;
  }
  return UpdateInstaller::ResultOutcome::None;
}

UpdateInstaller::ApplyResult makeApplyResult(UpdateInstaller::ApplyStatus p_status,
                                             const QString &p_message,
                                             const QString &p_targetVersion = QString()) {
  UpdateInstaller::ApplyResult r;
  r.status = p_status;
  r.message = p_message;
  r.targetVersion = p_targetVersion;
  return r;
}

} // namespace

// ===========================================================================
// Paths
// ===========================================================================

QString UpdateInstaller::stagingRoot(const QString &p_installDir) {
  return joinPath(p_installDir, c_stagingDirName);
}

QString UpdateInstaller::downloadDir(const QString &p_installDir) {
  return joinPath(stagingRoot(p_installDir), c_downloadDirName);
}

QString UpdateInstaller::stagedDir(const QString &p_installDir) {
  return joinPath(stagingRoot(p_installDir), c_stagedDirName);
}

QString UpdateInstaller::pendingPath(const QString &p_installDir) {
  return joinPath(stagingRoot(p_installDir), c_pendingFileName);
}

QString UpdateInstaller::journalPath(const QString &p_installDir) {
  return joinPath(stagingRoot(p_installDir), c_journalFileName);
}

QString UpdateInstaller::resultPath(const QString &p_installDir) {
  return joinPath(stagingRoot(p_installDir), c_resultFileName);
}

QString UpdateInstaller::backupRoot(const QString &p_installDir) {
  return joinPath(p_installDir, c_backupDirName);
}

// ===========================================================================
// Module path
// ===========================================================================

QString UpdateInstaller::exePathFromModulePath() {
#ifdef Q_OS_WIN
  // Deliberately NOT QCoreApplication::applicationFilePath(): this runs before
  // any Qt application object exists, and again after it has been destroyed.
  QVarLengthArray<wchar_t, MAX_PATH> buffer(MAX_PATH);
  for (;;) {
    const DWORD written =
        ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (written == 0) {
      return QString();
    }
    if (written < static_cast<DWORD>(buffer.size())) {
      return QDir::fromNativeSeparators(
          QString::fromWCharArray(buffer.data(), static_cast<int>(written)));
    }
    if (buffer.size() >= 32768) {
      return QString();
    }
    buffer.resize(buffer.size() * 2);
  }
#else
  // Portable-enough fallback for the non-Windows builds, which do not offer
  // self-update but must still compile and be testable.
  const QString self = QFileInfo(QStringLiteral("/proc/self/exe")).symLinkTarget();
  return self;
#endif
}

QString UpdateInstaller::installDirFromModulePath() {
  const QString exe = exePathFromModulePath();
  if (exe.isEmpty()) {
    return QString();
  }
  return QDir::cleanPath(QFileInfo(exe).absolutePath());
}

// ===========================================================================
// Eligibility probes
// ===========================================================================

bool UpdateInstaller::isInstallDirWritable(const QString &p_installDir) {
  if (p_installDir.isEmpty() || !isDirectory(p_installDir)) {
    return false;
  }

  const QString probe = joinPath(p_installDir, c_writeTestFileName);
  QFile::remove(probe);

  QFile file(probe);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  const bool written = file.write(QByteArrayLiteral("vnote")) == 5;
  file.close();

  const bool removed = QFile::remove(probe);
  return written && removed;
}

bool UpdateInstaller::isUnderProgramFiles(const QString &p_installDir) {
#ifdef Q_OS_WIN
  const QString target = QDir::cleanPath(QFileInfo(p_installDir).absoluteFilePath());
  const QStringList vars{QStringLiteral("ProgramFiles"), QStringLiteral("ProgramFiles(x86)"),
                         QStringLiteral("ProgramW6432")};
  for (const QString &var : vars) {
    const QString value = QString::fromLocal8Bit(qgetenv(var.toLocal8Bit().constData()));
    if (value.isEmpty()) {
      continue;
    }
    const QString root = QDir::cleanPath(QDir::fromNativeSeparators(value));
    if (root.isEmpty()) {
      continue;
    }
    if (target.compare(root, Qt::CaseInsensitive) == 0 ||
        target.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
#else
  Q_UNUSED(p_installDir);
  return false;
#endif
}

bool UpdateInstaller::isSameVolume(const QString &p_pathA, const QString &p_pathB) {
  if (p_pathA.isEmpty() || p_pathB.isEmpty()) {
    return false;
  }

  // QStorageInfo resolves the mount point that CONTAINS the path, so it works
  // for a path that does not exist yet only if an ancestor does. Walk up until
  // something exists.
  auto existingAncestor = [](const QString &p_path) {
    QString current = QDir::cleanPath(QFileInfo(p_path).absoluteFilePath());
    while (!current.isEmpty() && !pathExists(current)) {
      const QString parent = QFileInfo(current).absolutePath();
      if (parent == current) {
        break;
      }
      current = parent;
    }
    return current;
  };

  const QStorageInfo a(existingAncestor(p_pathA));
  const QStorageInfo b(existingAncestor(p_pathB));
  if (!a.isValid() || !b.isValid()) {
    return false;
  }
  return a.rootPath() == b.rootPath() && a.device() == b.device();
}

bool UpdateInstaller::probeAtomicRenameSupport(const QString &p_installDir) {
  if (g_forceAtomicRenameUnsupported) {
    return false;
  }

#ifdef Q_OS_WIN
  const QString probeDir = joinPath(stagingRoot(p_installDir), QStringLiteral("probe"));
  if (!QDir().mkpath(probeDir)) {
    return false;
  }

  const QString target = joinPath(probeDir, QStringLiteral("probe-target.tmp"));
  const QString aside = joinPath(probeDir, QStringLiteral("probe-target.aside"));
  const QString source = joinPath(probeDir, QStringLiteral("probe-source.tmp"));

  QFile::remove(target);
  QFile::remove(aside);
  QFile::remove(source);

  // A REAL PE image is required for the SEC_IMAGE mapping below. The running
  // module is guaranteed to be a valid PE for this architecture.
  const QString self = exePathFromModulePath();
  if (self.isEmpty() || !QFile::copy(self, target)) {
    QDir(probeDir).removeRecursively();
    return false;
  }

  {
    QFile src(source);
    if (!src.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      QDir(probeDir).removeRecursively();
      return false;
    }
    src.write(QByteArrayLiteral("vnote-executable-rename-probe"));
    src.close();
  }

  bool supported = false;

  const QString nativeTarget = QDir::toNativeSeparators(target);
  HANDLE targetHandle = ::CreateFileW(reinterpret_cast<const wchar_t *>(nativeTarget.utf16()),
                                      GENERIC_READ | GENERIC_EXECUTE,
                                      FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (targetHandle == INVALID_HANDLE_VALUE) {
    qWarning() << "update: rename probe could not open the probe target, win32 error"
               << static_cast<quint32>(::GetLastError());
  } else {
    // SEC_IMAGE maps the target exactly like a running executable. This is the
    // ONLY configuration that reproduces the restriction we care about, so a
    // probe without it would be worthless.
    HANDLE mapping =
        ::CreateFileMappingW(targetHandle, nullptr, PAGE_READONLY | SEC_IMAGE, 0, 0, nullptr);
    if (!mapping) {
      qWarning() << "update: rename probe could not create a SEC_IMAGE section, win32 error"
                 << static_cast<quint32>(::GetLastError());
    }

    void *view = nullptr;
    if (mapping) {
      view = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
      if (!view) {
        qWarning() << "update: rename probe could not map the section, win32 error"
                   << static_cast<quint32>(::GetLastError());
      }
    }

    if (view) {
      // Exactly the production sequence: move the mapped image aside, then move
      // a fresh file into the vacated name.
      DWORD err = ERROR_SUCCESS;
      if (!renameWin(target, aside, &err)) {
        qWarning() << "update: cannot move a mapped executable aside on this host, win32 error"
                   << static_cast<quint32>(err);
      } else if (!renameWin(source, target, &err)) {
        qWarning() << "update: cannot move a new executable into place on this host, win32 error"
                   << static_cast<quint32>(err);
      } else {
        supported = true;
      }
      ::UnmapViewOfFile(view);
    }

    if (mapping) {
      ::CloseHandle(mapping);
    }
    ::CloseHandle(targetHandle);
  }

  // Cleanup is best effort; a leftover probe directory is harmless and is
  // recreated/overwritten next time.
  QDir(probeDir).removeRecursively();
  return supported;
#else
  // POSIX rename(2) has the required semantics unconditionally, but the caller
  // still needs the staging directory to exist and be on the same volume.
  return isSameVolume(p_installDir, stagingRoot(p_installDir)) ||
         !pathExists(stagingRoot(p_installDir));
#endif
}

// ===========================================================================
// WebEngine child reaping
// ===========================================================================

QString UpdateInstaller::waitResultToString(WaitResult p_result) {
  switch (p_result) {
  case WaitResult::NoChildren:
    return QStringLiteral("no descendant processes");
  case WaitResult::Exited:
    return QStringLiteral("descendants exited");
  case WaitResult::TimedOut:
    return QStringLiteral("descendants still running after the timeout");
  case WaitResult::Error:
    return QStringLiteral("could not enumerate or open a descendant process");
  }
  return QStringLiteral("unknown");
}

#ifdef Q_OS_WIN
namespace {

struct ProcessEntry {
  DWORD pid = 0;
  DWORD parentPid = 0;
};

bool snapshotProcesses(QVector<ProcessEntry> *p_out) {
  HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) {
    return false;
  }

  PROCESSENTRY32W entry;
  entry.dwSize = sizeof(entry);
  if (!::Process32FirstW(snap, &entry)) {
    ::CloseHandle(snap);
    return false;
  }

  do {
    ProcessEntry e;
    e.pid = entry.th32ProcessID;
    e.parentPid = entry.th32ParentProcessID;
    p_out->append(e);
  } while (::Process32NextW(snap, &entry));

  ::CloseHandle(snap);
  return true;
}

QString imagePathOf(HANDLE p_process) {
  QVarLengthArray<wchar_t, MAX_PATH> buffer(MAX_PATH);
  for (;;) {
    DWORD size = static_cast<DWORD>(buffer.size());
    if (::QueryFullProcessImageNameW(p_process, 0, buffer.data(), &size)) {
      return QDir::fromNativeSeparators(
          QString::fromWCharArray(buffer.data(), static_cast<int>(size)));
    }
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer.size() >= 32768) {
      return QString();
    }
    buffer.resize(buffer.size() * 2);
  }
}

} // namespace
#endif // Q_OS_WIN

UpdateInstaller::WaitResult UpdateInstaller::waitForWebEngineChildren(const QString &p_installDir,
                                                                      int p_timeoutMs) {
#ifdef Q_OS_WIN
  const QString installRoot = QDir::cleanPath(QFileInfo(p_installDir).absoluteFilePath());
  const DWORD selfPid = ::GetCurrentProcessId();

  const QDateTime deadline = QDateTime::currentDateTimeUtc().addMSecs(p_timeoutMs);

  bool sawAnyChild = false;

  for (;;) {
    QVector<ProcessEntry> processes;
    if (!snapshotProcesses(&processes)) {
      return WaitResult::Error;
    }

    // Build the child adjacency and collect OUR descendants transitively.
    QHash<DWORD, QVector<DWORD>> children;
    for (const ProcessEntry &e : processes) {
      children[e.parentPid].append(e.pid);
    }

    QVector<DWORD> descendants;
    QVector<DWORD> frontier = children.value(selfPid);
    QSet<DWORD> seen;
    while (!frontier.isEmpty()) {
      const DWORD pid = frontier.takeLast();
      if (pid == selfPid || seen.contains(pid)) {
        continue;
      }
      seen.insert(pid);
      descendants.append(pid);
      for (const DWORD child : children.value(pid)) {
        frontier.append(child);
      }
    }

    // Retain handles so the wait below is meaningful: the snapshot alone is a
    // point-in-time table and cannot prove quiescence.
    QVector<HANDLE> handles;
    for (const DWORD pid : descendants) {
      HANDLE h = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
      if (!h) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_INVALID_PARAMETER) {
          // The process exited between the snapshot and the open. Not an error.
          continue;
        }
        for (HANDLE open : handles) {
          ::CloseHandle(open);
        }
        return WaitResult::Error;
      }

      // Confirm the image by FULL PATH. Never match by name alone, and never
      // match system-wide: only processes running out of our own install tree.
      const QString image = imagePathOf(h);
      const bool ours =
          !image.isEmpty() && (image.compare(installRoot, Qt::CaseInsensitive) == 0 ||
                               image.startsWith(installRoot + QLatin1Char('/'), Qt::CaseInsensitive));
      if (!ours) {
        ::CloseHandle(h);
        continue;
      }
      handles.append(h);
    }

    if (handles.isEmpty()) {
      return sawAnyChild ? WaitResult::Exited : WaitResult::NoChildren;
    }
    sawAnyChild = true;

    const qint64 remaining = QDateTime::currentDateTimeUtc().msecsTo(deadline);
    if (remaining <= 0) {
      for (HANDLE h : handles) {
        ::CloseHandle(h);
      }
      return WaitResult::TimedOut;
    }

    // WaitForMultipleObjects caps at MAXIMUM_WAIT_OBJECTS handles; chunk it.
    bool timedOut = false;
    const int handleCount = static_cast<int>(handles.size());
    for (int offset = 0; offset < handleCount; offset += MAXIMUM_WAIT_OBJECTS) {
      const DWORD chunk =
          static_cast<DWORD>(qMin<int>(MAXIMUM_WAIT_OBJECTS, handleCount - offset));
      const qint64 left = QDateTime::currentDateTimeUtc().msecsTo(deadline);
      if (left <= 0) {
        timedOut = true;
        break;
      }
      const DWORD waited = ::WaitForMultipleObjects(chunk, handles.data() + offset, TRUE,
                                                    static_cast<DWORD>(qMin<qint64>(left, 60000)));
      if (waited == WAIT_TIMEOUT) {
        timedOut = true;
        break;
      }
      if (waited == WAIT_FAILED) {
        for (HANDLE h : handles) {
          ::CloseHandle(h);
        }
        return WaitResult::Error;
      }
    }

    for (HANDLE h : handles) {
      ::CloseHandle(h);
    }

    if (timedOut) {
      return WaitResult::TimedOut;
    }
    // Re-enumerate: a descendant may have spawned another one while we waited.
  }
#else
  Q_UNUSED(p_installDir);
  Q_UNUSED(p_timeoutMs);
  return WaitResult::NoChildren;
#endif
}

// ===========================================================================
// PendingPlan
// ===========================================================================

bool UpdateInstaller::PendingPlan::isValid() const {
  if (targetVersion.isEmpty() || variant.isEmpty() || executablePath.isEmpty()) {
    return false;
  }
  if (targetManifest.isEmpty()) {
    return false;
  }
  if (staged.isEmpty() && deletions.isEmpty()) {
    return false;
  }
  return true;
}

QJsonObject UpdateInstaller::PendingPlan::toJson() const {
  QJsonObject obj;
  obj[QStringLiteral("schema")] = c_schema;
  obj[QStringLiteral("targetVersion")] = targetVersion;
  obj[QStringLiteral("variant")] = variant;
  obj[QStringLiteral("executablePath")] = executablePath;
  obj[QStringLiteral("targetManifest")] = targetManifest;

  auto toArray = [](const QStringList &p_list) {
    QJsonArray arr;
    for (const QString &s : p_list) {
      arr.append(s);
    }
    return arr;
  };
  obj[QStringLiteral("staged")] = toArray(staged);
  obj[QStringLiteral("deletions")] = toArray(deletions);
  obj[QStringLiteral("conflicts")] = toArray(conflicts);
  return obj;
}

UpdateInstaller::PendingPlan UpdateInstaller::PendingPlan::fromJson(const QJsonObject &p_obj,
                                                                    QString *p_error) {
  PendingPlan plan;

  auto fail = [&](const QString &p_message) {
    if (p_error) {
      *p_error = p_message;
    }
    return PendingPlan();
  };

  if (p_obj.value(QStringLiteral("schema")).toInt() != c_schema) {
    return fail(QStringLiteral("unsupported pending.json schema"));
  }

  plan.targetVersion = p_obj.value(QStringLiteral("targetVersion")).toString();
  plan.variant = p_obj.value(QStringLiteral("variant")).toString();
  plan.executablePath =
      UpdateManifest::normalizePath(p_obj.value(QStringLiteral("executablePath")).toString());
  plan.targetManifest = p_obj.value(QStringLiteral("targetManifest")).toObject();

  if (plan.executablePath.isEmpty()) {
    return fail(QStringLiteral("missing or unsafe executablePath"));
  }

  auto readPaths = [&](const QString &p_key, QStringList *p_out) -> bool {
    const QJsonValue val = p_obj.value(p_key);
    if (val.isUndefined() || val.isNull()) {
      return true;
    }
    if (!val.isArray()) {
      return false;
    }
    for (const QJsonValue &v : val.toArray()) {
      const QString normalized = UpdateManifest::normalizePath(v.toString());
      if (normalized.isEmpty() || UpdateManifest::isReservedPath(normalized)) {
        return false;
      }
      p_out->append(normalized);
    }
    return true;
  };

  if (!readPaths(QStringLiteral("staged"), &plan.staged) ||
      !readPaths(QStringLiteral("deletions"), &plan.deletions) ||
      !readPaths(QStringLiteral("conflicts"), &plan.conflicts)) {
    return fail(QStringLiteral("pending.json contains an unsafe path"));
  }

  if (!plan.isValid()) {
    return fail(QStringLiteral("pending.json is structurally incomplete"));
  }

  return plan;
}

bool UpdateInstaller::writePending(const QString &p_installDir, const PendingPlan &p_plan) {
  if (!p_plan.isValid()) {
    return false;
  }
  return writeJsonAtomically(pendingPath(p_installDir), p_plan.toJson());
}

UpdateInstaller::PendingPlan UpdateInstaller::readPending(const QString &p_installDir,
                                                          QString *p_error) {
  QJsonObject obj;
  if (!readJson(pendingPath(p_installDir), &obj)) {
    if (p_error) {
      *p_error = QStringLiteral("pending.json is absent or unreadable");
    }
    return PendingPlan();
  }
  return PendingPlan::fromJson(obj, p_error);
}

bool UpdateInstaller::clearPending(const QString &p_installDir) {
  const QString path = pendingPath(p_installDir);
  if (!pathExists(path)) {
    return true;
  }
  return QFile::remove(path);
}

bool UpdateInstaller::removeStagingRoot(const QString &p_installDir) {
  return removeRecursively(stagingRoot(p_installDir));
}

// ===========================================================================
// result.json
// ===========================================================================

bool UpdateInstaller::writeResult(const QString &p_installDir, ResultOutcome p_outcome,
                                  const QString &p_reason, const QString &p_detail,
                                  const QString &p_targetVersion) {
  QJsonObject obj;
  obj[QStringLiteral("schema")] = 1;
  obj[QStringLiteral("outcome")] = outcomeToString(p_outcome);
  obj[QStringLiteral("reason")] = p_reason;
  obj[QStringLiteral("detail")] = p_detail;
  obj[QStringLiteral("targetVersion")] = p_targetVersion;
  obj[QStringLiteral("timestamp")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  return writeJsonAtomically(resultPath(p_installDir), obj);
}

bool UpdateInstaller::writeRetryableResult(const QString &p_installDir, const QString &p_reason) {
  return writeResult(p_installDir, ResultOutcome::Retryable, p_reason);
}

bool UpdateInstaller::writeSpawnFailure(const QString &p_installDir) {
  return writeResult(p_installDir, ResultOutcome::SpawnFailed,
                     QStringLiteral("could not start the updated VNote executable"));
}

UpdateInstaller::StoredResult UpdateInstaller::readResult(const QString &p_installDir) {
  StoredResult result;
  QJsonObject obj;
  if (!readJson(resultPath(p_installDir), &obj)) {
    return result;
  }
  if (obj.value(QStringLiteral("schema")).toInt() != 1) {
    return result;
  }
  result.outcome = outcomeFromString(obj.value(QStringLiteral("outcome")).toString());
  result.reason = obj.value(QStringLiteral("reason")).toString();
  result.detail = obj.value(QStringLiteral("detail")).toString();
  result.targetVersion = obj.value(QStringLiteral("targetVersion")).toString();
  result.timestamp = obj.value(QStringLiteral("timestamp")).toString();
  return result;
}

bool UpdateInstaller::clearResult(const QString &p_installDir) {
  const QString path = resultPath(p_installDir);
  if (!pathExists(path)) {
    return true;
  }
  return QFile::remove(path);
}

// ===========================================================================
// Hashing
// ===========================================================================

QString UpdateInstaller::hashFile(const QString &p_path) {
  QFile file(p_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QString();
  }

  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file)) {
    file.close();
    return QString();
  }
  file.close();
  return QString::fromLatin1(hash.result().toHex());
}

// ===========================================================================
// Transaction engine
// ===========================================================================
namespace {

// Everything the forward/reverse/recovery routines need, resolved once.
struct TxContext {
  QString installDir;
  QString stagedRoot;
  QString backupDirAbs; // <installDir>/<journal.backupDir>
};

QString targetPathOf(const TxContext &p_ctx, const QString &p_relative) {
  return joinPath(p_ctx.installDir, p_relative);
}

QString stagedPathOf(const TxContext &p_ctx, const QString &p_relative) {
  return joinPath(p_ctx.stagedRoot, p_relative);
}

QString backupPathOf(const TxContext &p_ctx, const QString &p_relative) {
  return joinPath(p_ctx.backupDirAbs, p_relative);
}

// Moves an existing install-tree entry into the backup directory.
bool backupExisting(const TxContext &p_ctx, const QString &p_relative) {
  const QString from = targetPathOf(p_ctx, p_relative);
  if (!pathExists(from)) {
    // Already absent: nothing to preserve. Idempotent on replay.
    return true;
  }
  const QString to = backupPathOf(p_ctx, p_relative);
  if (pathExists(to)) {
    // A previous attempt already backed this up; do not clobber the original.
    return removeRecursively(from);
  }
  return renamePath(from, to, p_relative);
}

bool restoreFromBackup(const TxContext &p_ctx, const QString &p_relative) {
  const QString from = backupPathOf(p_ctx, p_relative);
  if (!pathExists(from)) {
    // Nothing to restore; treat as already-restored so recovery converges.
    return true;
  }
  const QString to = targetPathOf(p_ctx, p_relative);
  if (pathExists(to)) {
    if (!removeRecursively(to)) {
      return false;
    }
  }
  return renamePath(from, to, p_relative);
}

// Removes directories this operation created, innermost first, and only when
// they are empty.
void removeCreatedDirs(const TxContext &p_ctx, const JournalOp &p_op) {
  QStringList dirs = p_op.createdDirs;
  // createdDirs is recorded outermost-first; remove in reverse.
  for (int i = dirs.size() - 1; i >= 0; --i) {
    removeEmptyDir(targetPathOf(p_ctx, dirs.at(i)));
  }
}

// --- ReplaceExecutable state machine --------------------------------------

enum class ExecClassification {
  // Canonical still holds the old image.
  CanonicalOld,
  // Canonical already holds the new image.
  CanonicalNew,
  // Canonical is missing or matches neither hash.
  CanonicalUnknown,
};

ExecClassification classifyCanonical(const QString &p_path, const QString &p_oldHash,
                                     const QString &p_newHash) {
  if (!pathExists(p_path)) {
    return ExecClassification::CanonicalUnknown;
  }
  const QString hash = UpdateInstaller::hashFile(p_path);
  if (hash.isEmpty()) {
    return ExecClassification::CanonicalUnknown;
  }
  if (hash.compare(p_newHash, Qt::CaseInsensitive) == 0) {
    return ExecClassification::CanonicalNew;
  }
  if (hash.compare(p_oldHash, Qt::CaseInsensitive) == 0) {
    return ExecClassification::CanonicalOld;
  }
  return ExecClassification::CanonicalUnknown;
}

bool backupMatchesOld(const TxContext &p_ctx, const JournalOp &p_op) {
  const QString backup = backupPathOf(p_ctx, p_op.path);
  if (!pathExists(backup)) {
    return false;
  }
  return UpdateInstaller::hashFile(backup).compare(p_op.expectedOldHash, Qt::CaseInsensitive) == 0;
}

bool stagedMatchesNew(const TxContext &p_ctx, const JournalOp &p_op) {
  const QString staged = stagedPathOf(p_ctx, p_op.path);
  if (!pathExists(staged)) {
    return false;
  }
  return UpdateInstaller::hashFile(staged).compare(p_op.expectedNewHash, Qt::CaseInsensitive) == 0;
}

// Forward step 1: move the CURRENTLY RUNNING executable out of the canonical
// name and into the backup directory.
//
// This is a rename, not a copy: a copy would leave the mapped image sitting on
// the canonical path, and step 2 would then have to unlink a mapped image --
// which Windows refuses (see the table above). After this step the canonical
// name does not exist; that window is closed by execMoveIn.
bool execMoveAside(const TxContext &p_ctx, const JournalOp &p_op, QString *p_error) {
  const QString canonical = targetPathOf(p_ctx, p_op.path);
  const QString backup = backupPathOf(p_ctx, p_op.path);

  if (backupMatchesOld(p_ctx, p_op)) {
    // Replayed: the move already happened.
    return true;
  }

  if (!pathExists(canonical)) {
    *p_error = QStringLiteral("the executable '%1' is gone and no valid backup exists")
                   .arg(canonical);
    return false;
  }

  if (!ensureParentDir(backup)) {
    *p_error = QStringLiteral("cannot create the backup directory for '%1'").arg(backup);
    return false;
  }
  QFile::remove(backup);

  if (!executableRename(canonical, backup, p_error)) {
    return false;
  }

  if (!backupMatchesOld(p_ctx, p_op)) {
    *p_error = QStringLiteral("the executable backup at '%1' does not verify").arg(backup);
    return false;
  }
  return true;
}

// Forward step 2: move the new executable into the now-vacant canonical name.
bool execMoveIn(const TxContext &p_ctx, const JournalOp &p_op, QString *p_error) {
  const QString canonical = targetPathOf(p_ctx, p_op.path);
  const QString staged = stagedPathOf(p_ctx, p_op.path);

  if (classifyCanonical(canonical, p_op.expectedOldHash, p_op.expectedNewHash) ==
      ExecClassification::CanonicalNew) {
    // Replayed: already in place.
    return true;
  }

  if (!stagedMatchesNew(p_ctx, p_op)) {
    *p_error =
        QStringLiteral("staged executable '%1' is missing or does not match the expected hash")
            .arg(staged);
    return false;
  }

  return executableRename(staged, canonical, p_error);
}

// Reverse. NEVER remove() or generic-rename() the canonical executable: it goes
// back through the same two-rename dance, in the opposite order.
bool execReverse(const TxContext &p_ctx, const JournalOp &p_op, QString *p_error) {
  const QString canonical = targetPathOf(p_ctx, p_op.path);
  const QString backup = backupPathOf(p_ctx, p_op.path);
  const QString staged = stagedPathOf(p_ctx, p_op.path);

  if (!pathExists(backup)) {
    *p_error = QStringLiteral("backup executable '%1' is gone; cannot revert").arg(backup);
    return false;
  }

  if (classifyCanonical(canonical, p_op.expectedOldHash, p_op.expectedNewHash) ==
      ExecClassification::CanonicalNew) {
    // Move the NEW image back to staging so the update stays retryable, which
    // also vacates the canonical name for the backup.
    if (ensureParentDir(staged)) {
      QFile::remove(staged);
    }
    if (!executableRename(canonical, staged, p_error)) {
      return false;
    }
  }

  return executableRename(backup, canonical, p_error);
}

// --- Generic reverse ------------------------------------------------------
//
// Reverse depends on how far forward the op got, which is exactly what
// JournalOp::State records:
//
//   Intent    nothing durable happened          -> nothing to undo
//   BackedUp  backup taken / directories made   -> restore backup, drop dirs
//   Done      fully applied                     -> undo the move too
//
// CRITICAL: reverse must also be REPLAY-SAFE. `rollback()` can only journal
// `Reverted` AFTER the filesystem work returns, so a crash in between replays
// this function against a partially-reversed tree. Naively re-running the
// `Done` path would then move the freshly RESTORED original into staging and
// find no backup left to put back, deleting a production file outright.
//
// The invariant that makes replay safe for Replace/Delete/ConflictRemove is:
//
//     the backup exists  <=>  the restore has NOT happened yet
//
// so the presence of the backup, not the journal state, decides whether there
// is anything left to undo.
bool performReverse(const TxContext &p_ctx, const JournalOp &p_op, QString *p_error) {
  switch (p_op.type) {
  case JournalOp::Type::ConflictRemove:
  case JournalOp::Type::Delete:
    // No move phase: BackedUp and Done are the same on-disk situation, and
    // restoreFromBackup() is already a no-op once the backup is consumed.
    if (!withRetry([&]() { return restoreFromBackup(p_ctx, p_op.path); })) {
      *p_error = QStringLiteral("could not restore '%1' from backup").arg(p_op.path);
      return false;
    }
    return true;

  case JournalOp::Type::Add: {
    // No backup was ever taken, so the only undo is to take the new file back
    // out. Idempotent: a replay finds the target already gone and skips.
    if (p_op.state == JournalOp::State::Done) {
      const QString target = targetPathOf(p_ctx, p_op.path);
      const QString staged = stagedPathOf(p_ctx, p_op.path);
      if (pathExists(target)) {
        if (!withRetry([&]() {
              if (pathExists(staged) && !removeRecursively(staged)) {
                return false;
              }
              return renamePath(target, staged, p_op.path);
            })) {
          *p_error = QStringLiteral("could not move '%1' back to staging").arg(p_op.path);
          return false;
        }
      }
    }
    removeCreatedDirs(p_ctx, p_op);
    return true;
  }

  case JournalOp::Type::Replace: {
    const QString backup = backupPathOf(p_ctx, p_op.path);

    if (pathExists(backup)) {
      // The restore has NOT happened yet, so whatever sits at the target is
      // the NEW file (or nothing, if a previous attempt died between the two
      // moves). Only in this branch is it safe to move the target away.
      if (p_op.state == JournalOp::State::Done) {
        const QString target = targetPathOf(p_ctx, p_op.path);
        const QString staged = stagedPathOf(p_ctx, p_op.path);
        if (pathExists(target)) {
          // Move the NEW file back to staging so the update stays retryable.
          if (!withRetry([&]() {
                if (pathExists(staged) && !removeRecursively(staged)) {
                  return false;
                }
                return renamePath(target, staged, p_op.path);
              })) {
            *p_error = QStringLiteral("could not move '%1' back to staging").arg(p_op.path);
            return false;
          }
        }
      }

      if (!withRetry([&]() { return restoreFromBackup(p_ctx, p_op.path); })) {
        *p_error = QStringLiteral("could not restore '%1' from backup").arg(p_op.path);
        return false;
      }

      if (faultFires(UpdateInstaller::FaultPoint::RollbackAfterBackupRestored, -1)) {
        // Models a kill in the window where the restore is DONE on disk but the
        // journal still says `Done`. Replaying from here must be a no-op.
        *p_error = QStringLiteral("__fault__");
        return false;
      }
    }
    // else: the backup is already consumed, so the original is back at the
    // target and there is nothing left to undo. Touching it here is exactly
    // the bug this branch exists to prevent.

    removeCreatedDirs(p_ctx, p_op);
    return true;
  }

  case JournalOp::Type::ReplaceExecutable:
    return execReverse(p_ctx, p_op, p_error);
  }

  *p_error = QStringLiteral("unknown operation type");
  return false;
}

// Bottom-up removal of directories that became empty. Best effort.
void pruneEmptyDirectories(const QString &p_installDir) {
  QStringList dirs;
  QDirIterator it(p_installDir, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString dir = it.next();
    const QString relative = QDir(p_installDir).relativeFilePath(dir);
    if (relative.startsWith(c_stagingDirName) || relative.startsWith(c_backupDirName)) {
      continue;
    }
    dirs.append(dir);
  }

  // Deepest first.
  std::sort(dirs.begin(), dirs.end(),
            [](const QString &a, const QString &b) { return a.size() > b.size(); });
  for (const QString &dir : dirs) {
    removeEmptyDir(dir);
  }
}

// Persists a post-mutation checkpoint.
//
// A failure here is NOT something the transaction may continue through: the
// next irreversible rename would run while the on-disk journal still describes
// the PREVIOUS state, which destroys the one invariant the journal exists to
// provide ("every mutation has a durable checkpoint recorded before it"). Disk
// full, an antivirus lock on journal.json, or a lost network volume all land
// here. Stop and hand the install to a human instead.
bool commitCheckpoint(const QString &p_installDir, Journal &p_journal) {
  if (saveJournal(p_installDir, p_journal)) {
    return true;
  }

  qCritical() << "VNote could not persist the update journal at"
              << UpdateInstaller::journalPath(p_installDir)
              << "- stopping the update part-way rather than mutating further";

  p_journal.phase = Journal::Phase::ManualRecovery;
  // Best effort: if this write fails too, the previous journal contents plus
  // the backups are still on disk and RECOVERY.txt still applies.
  saveJournal(p_installDir, p_journal);
  UpdateInstaller::writeResult(
      p_installDir, UpdateInstaller::ResultOutcome::ManualRecovery,
      QStringLiteral("could not persist the update journal"),
      QStringLiteral("the update was stopped part-way; see RECOVERY.txt under .vnote-old"),
      p_journal.targetVersion);
  return false;
}

UpdateInstaller::ApplyResult checkpointFailure(const Journal &p_journal) {
  return makeApplyResult(UpdateInstaller::ApplyStatus::RollbackFailed,
                         QStringLiteral("could not persist the update journal"),
                         p_journal.targetVersion);
}

// Rolls the transaction back from the current journal state.
UpdateInstaller::ApplyResult rollback(const QString &p_installDir, Journal &p_journal,
                                      const TxContext &p_ctx, const QString &p_cause) {
  p_journal.phase = Journal::Phase::RollingBack;
  if (!saveJournal(p_installDir, p_journal)) {
    // The phase change MUST be durable before any reverse runs: otherwise a
    // crash mid-rollback leaves the journal saying APPLYING and recovery would
    // replay FORWARD over a half-reversed tree.
    qCritical() << "VNote could not record the rollback phase; leaving the install untouched "
                   "for manual recovery";
    p_journal.phase = Journal::Phase::ManualRecovery;
    saveJournal(p_installDir, p_journal);
    UpdateInstaller::writeResult(p_installDir, UpdateInstaller::ResultOutcome::ManualRecovery,
                                 p_cause,
                                 QStringLiteral("could not record the rollback phase"),
                                 p_journal.targetVersion);
    return makeApplyResult(UpdateInstaller::ApplyStatus::RollbackFailed, p_cause,
                           p_journal.targetVersion);
  }

  if (faultFires(UpdateInstaller::FaultPoint::RollbackAfterPhaseCommit, -1)) {
    return makeApplyResult(UpdateInstaller::ApplyStatus::FaultInjected,
                           QStringLiteral("fault injected after the rollback phase commit"),
                           p_journal.targetVersion);
  }

  bool allReverted = true;
  QString firstFailure;

  for (int i = p_journal.ops.size() - 1; i >= 0; --i) {
    JournalOp &op = p_journal.ops[i];
    if (op.state == JournalOp::State::Intent || op.state == JournalOp::State::Reverted) {
      // Nothing durable happened for this op (or it is already undone).
      continue;
    }

    QString error;
    if (!performReverse(p_ctx, op, &error)) {
      if (error == QLatin1String("__fault__")) {
        // A fault point fired inside the reverse. Model a kill: return without
        // journalling anything, so the next recoverInterrupted() replays this
        // very operation from a half-reversed tree.
        return makeApplyResult(UpdateInstaller::ApplyStatus::FaultInjected,
                               QStringLiteral("fault injected during the reverse of op %1").arg(i),
                               p_journal.targetVersion);
      }
      allReverted = false;
      if (firstFailure.isEmpty()) {
        firstFailure = error;
      }
      // Keep going: undo as much as possible.
      continue;
    }

    op.state = JournalOp::State::Reverted;
    if (!saveJournal(p_installDir, p_journal)) {
      // The reverse succeeded but we cannot record it. Continuing would risk
      // replaying it later against an already-reversed tree.
      qCritical() << "VNote could not persist the update journal during rollback";
      allReverted = false;
      if (firstFailure.isEmpty()) {
        firstFailure = QStringLiteral("could not persist the update journal during rollback");
      }
      break;
    }

    if (faultFires(UpdateInstaller::FaultPoint::RollbackAfterRestore, i)) {
      return makeApplyResult(UpdateInstaller::ApplyStatus::FaultInjected,
                             QStringLiteral("fault injected after restoring op %1").arg(i),
                             p_journal.targetVersion);
    }
  }

  if (allReverted) {
    p_journal.phase = Journal::Phase::RolledBack;
    if (!saveJournal(p_installDir, p_journal)) {
      // Survivable: the live journal is removed a few lines below and a
      // terminal copy is written beside the backups from the in-memory object,
      // so recovery still converges. Log it rather than hiding it.
      qWarning() << "VNote could not persist the terminal ROLLED_BACK journal";
    }
    UpdateInstaller::writeResult(p_installDir, UpdateInstaller::ResultOutcome::Failed, p_cause,
                                 QStringLiteral("the previous version was restored"),
                                 p_journal.targetVersion);

    // Leave a terminal copy beside the backups so cleanupOldBackups can reclaim
    // them, then DROP the live journal: the staged tree is intact, so the very
    // next quit may retry the update from scratch. Keeping the terminal journal
    // in .vnote-update/ would make applyPending() delegate to recovery forever.
    if (isDirectory(p_ctx.backupDirAbs)) {
      writeJsonAtomically(joinPath(p_ctx.backupDirAbs, c_journalFileName),
                          journalToJson(p_journal));
    }
    QFile::remove(UpdateInstaller::journalPath(p_installDir));

    return makeApplyResult(UpdateInstaller::ApplyStatus::RolledBack, p_cause,
                           p_journal.targetVersion);
  }

  // Residual risk 2, tier 3: contention also blocked the rollback. Preserve the
  // journal and the backups, and point a human at RECOVERY.txt.
  p_journal.phase = Journal::Phase::ManualRecovery;
  saveJournal(p_installDir, p_journal);
  qCritical() << "VNote update rollback failed;" << p_cause << "|" << firstFailure
              << "- see RECOVERY.txt in" << joinPath(p_installDir, p_journal.backupDir);
  UpdateInstaller::writeResult(p_installDir, UpdateInstaller::ResultOutcome::ManualRecovery,
                               p_cause, firstFailure, p_journal.targetVersion);
  return makeApplyResult(UpdateInstaller::ApplyStatus::RollbackFailed, firstFailure,
                         p_journal.targetVersion);
}

// Idempotent tail of a committed transaction. Split out so recoverInterrupted()
// can finish a commit that was interrupted between "journal COMMITTED" and
// "staging removed".
void finalizeCommit(const QString &p_installDir, const Journal &p_journal,
                    const TxContext &p_ctx) {
  // Keep the terminal journal alongside the backups so cleanupOldBackups can
  // tell a finished transaction from an abandoned one AFTER .vnote-update/ is
  // gone.
  if (isDirectory(p_ctx.backupDirAbs)) {
    const QString journalCopy = joinPath(p_ctx.backupDirAbs, c_journalFileName);
    QFile::remove(journalCopy);
    writeJsonAtomically(journalCopy, journalToJson(p_journal));
  }

  UpdateInstaller::removeStagingRoot(p_installDir);

  // result.json is written AFTER the staging root is removed, so it is the only
  // thing left in .vnote-update/. The next launch reads it, turns it into a
  // notification, and clears it.
  UpdateInstaller::writeResult(p_installDir, UpdateInstaller::ResultOutcome::Applied,
                               QStringLiteral("update applied"), QString(),
                               p_journal.targetVersion);
}

// Commits manifest.json, prunes empty directories, marks the journal COMMITTED
// and removes .vnote-update/. Backups are retained until the next successful
// launch (see cleanupOldBackups).
UpdateInstaller::ApplyResult commitTransaction(const QString &p_installDir, Journal &p_journal,
                                               const TxContext &p_ctx) {
  if (faultFires(UpdateInstaller::FaultPoint::BeforeManifestCommit, -1)) {
    return makeApplyResult(UpdateInstaller::ApplyStatus::FaultInjected,
                           QStringLiteral("fault injected before the manifest commit"),
                           p_journal.targetVersion);
  }

  const QString manifestPath = joinPath(p_installDir, UpdateManifest::manifestFileName());
  if (!writeJsonAtomically(manifestPath, p_journal.targetManifest)) {
    // A manifest commit failure is a TRANSACTION failure: without the new
    // manifest the install cannot serve as a delta base and every later update
    // would silently degrade to the full package.
    return rollback(p_installDir, p_journal, p_ctx,
                    QStringLiteral("could not write the new manifest.json"));
  }

  if (faultFires(UpdateInstaller::FaultPoint::AfterManifestCommit, -1)) {
    return makeApplyResult(UpdateInstaller::ApplyStatus::FaultInjected,
                           QStringLiteral("fault injected after the manifest commit"),
                           p_journal.targetVersion);
  }

  pruneEmptyDirectories(p_installDir);

  p_journal.phase = Journal::Phase::Committed;
  if (!saveJournal(p_installDir, p_journal)) {
    // The tree and manifest.json are already correct at this point, so
    // finishing the commit is safer than aborting. finalizeCommit writes the
    // terminal copy beside the backups from the in-memory journal anyway.
    qWarning() << "VNote could not persist the terminal COMMITTED journal";
  }

  finalizeCommit(p_installDir, p_journal, p_ctx);

  return makeApplyResult(UpdateInstaller::ApplyStatus::Committed,
                         QStringLiteral("update applied"), p_journal.targetVersion);
}

// Drives the forward pass from wherever the journal currently is. Shared by
// applyPending() and recoverInterrupted().
//
// Each ordinary operation moves through four durable checkpoints so a kill at
// ANY point leaves the journal describing exactly what happened:
//
//   Intent -> (dirs recorded + created) -> backup -> BackedUp
//          -> staged->target move -> Done
UpdateInstaller::ApplyResult runForward(const QString &p_installDir, Journal &p_journal,
                                        const TxContext &p_ctx) {
  for (int i = 0; i < p_journal.ops.size(); ++i) {
    JournalOp &op = p_journal.ops[i];
    if (op.state == JournalOp::State::Done) {
      continue;
    }

    if (faultFires(UpdateInstaller::FaultPoint::AfterIntentCommit, i)) {
      return makeApplyResult(UpdateInstaller::ApplyStatus::FaultInjected,
                             QStringLiteral("fault injected after the intent commit of op %1").arg(i),
                             p_journal.targetVersion);
    }

    // --- The executable: a distinct state machine, never the generic path ---
    //
    // Two renames, because Windows refuses to unlink the name of a file with an
    // active IMAGE section (see the table above execMoveAside):
    //   Intent    -> canonical moved into the backup   -> BackedUp
    //   BackedUp  -> staged moved onto the canonical    -> Done
    if (op.type == JournalOp::Type::ReplaceExecutable) {
      if (faultFires(UpdateInstaller::FaultPoint::ExecAfterIntentCommit, i)) {
        return makeApplyResult(
            UpdateInstaller::ApplyStatus::FaultInjected,
            QStringLiteral("fault injected after the executable intent commit"),
            p_journal.targetVersion);
      }

      if (op.state == JournalOp::State::Intent) {
        QString error;
        if (!withRetry([&]() { return execMoveAside(p_ctx, op, &error); })) {
          return rollback(p_installDir, p_journal, p_ctx, error);
        }
        op.state = JournalOp::State::BackedUp;
        if (!commitCheckpoint(p_installDir, p_journal)) {
          return checkpointFailure(p_journal);
        }
      }

      if (faultFires(UpdateInstaller::FaultPoint::ExecAfterMoveAside, i)) {
        // The nastiest reachable state: the canonical executable name does not
        // exist right now. Recovery must be able to converge from here.
        return makeApplyResult(UpdateInstaller::ApplyStatus::FaultInjected,
                               QStringLiteral("fault injected after the executable move-aside"),
                               p_journal.targetVersion);
      }

      QString error;
      if (!withRetry([&]() { return execMoveIn(p_ctx, op, &error); })) {
        return rollback(p_installDir, p_journal, p_ctx, error);
      }

      op.state = JournalOp::State::Done;
      if (!commitCheckpoint(p_installDir, p_journal)) {
        return checkpointFailure(p_journal);
      }

      if (faultFires(UpdateInstaller::FaultPoint::ExecAfterMoveIn, i)) {
        return makeApplyResult(UpdateInstaller::ApplyStatus::FaultInjected,
                               QStringLiteral("fault injected after the executable move-in"),
                               p_journal.targetVersion);
      }
      continue;
    }

    // --- Ordinary operations ------------------------------------------------
    const bool movesAFile =
        op.type == JournalOp::Type::Add || op.type == JournalOp::Type::Replace;

    if (movesAFile && op.state == JournalOp::State::Intent) {
      // Record the directories this op will create BEFORE creating them, so
      // rollback knows exactly what to remove.
      op.createdDirs = missingParentDirs(p_ctx.installDir, op.path);
      if (!saveJournal(p_installDir, p_journal)) {
        return rollback(p_installDir, p_journal, p_ctx,
                        QStringLiteral("cannot write the update journal"));
      }
      if (!op.createdDirs.isEmpty()) {
        const QString parent = QFileInfo(targetPathOf(p_ctx, op.path)).absolutePath();
        if (!QDir().mkpath(parent)) {
          return rollback(p_installDir, p_journal, p_ctx,
                          QStringLiteral("cannot create directory '%1'").arg(parent));
        }
      }
    }

    if (op.state == JournalOp::State::Intent) {
      // Move the existing entry (if any) into the backup directory. Add ops
      // have no existing entry, so this is a no-op for them.
      if (op.type != JournalOp::Type::Add) {
        if (!withRetry([&]() { return backupExisting(p_ctx, op.path); })) {
          return rollback(p_installDir, p_journal, p_ctx,
                          QStringLiteral("could not move '%1' out of the way").arg(op.path));
        }
      }

      op.state = JournalOp::State::BackedUp;
      if (!commitCheckpoint(p_installDir, p_journal)) {
        return checkpointFailure(p_journal);
      }

      if (faultFires(UpdateInstaller::FaultPoint::AfterBackupSyscall, i)) {
        return makeApplyResult(UpdateInstaller::ApplyStatus::FaultInjected,
                               QStringLiteral("fault injected after the backup syscall of op %1")
                                   .arg(i),
                               p_journal.targetVersion);
      }
    }

    if (movesAFile) {
      const QString from = stagedPathOf(p_ctx, op.path);
      const QString to = targetPathOf(p_ctx, op.path);
      if (!pathExists(from)) {
        if (!pathExists(to)) {
          return rollback(p_installDir, p_journal, p_ctx,
                          QStringLiteral("staged file '%1' is missing").arg(op.path));
        }
        // Replayed after the move already landed.
      } else if (!withRetry([&]() {
                   if (pathExists(to) && !removeRecursively(to)) {
                     return false;
                   }
                   return renamePath(from, to, op.path);
                 })) {
        return rollback(p_installDir, p_journal, p_ctx,
                        QStringLiteral("could not move staged '%1' into place").arg(op.path));
      }

      if (faultFires(UpdateInstaller::FaultPoint::AfterStagedToTargetMove, i)) {
        return makeApplyResult(
            UpdateInstaller::ApplyStatus::FaultInjected,
            QStringLiteral("fault injected after the staged->target move of op %1").arg(i),
            p_journal.targetVersion);
      }
    }

    op.state = JournalOp::State::Done;
    if (!commitCheckpoint(p_installDir, p_journal)) {
      return checkpointFailure(p_journal);
    }

    if (faultFires(UpdateInstaller::FaultPoint::AfterDoneCommit, i)) {
      return makeApplyResult(UpdateInstaller::ApplyStatus::FaultInjected,
                             QStringLiteral("fault injected after the done commit of op %1").arg(i),
                             p_journal.targetVersion);
    }
  }

  return commitTransaction(p_installDir, p_journal, p_ctx);
}

} // namespace

// ===========================================================================
// applyPending
// ===========================================================================

UpdateInstaller::ApplyResult UpdateInstaller::applyPending(const QString &p_installDir) {
  // A journal already present means a previous apply was interrupted (or
  // committed but not yet cleaned up); recovery owns that case and must run
  // first. Delegating unconditionally keeps this the single entry point for
  // "there is already a transaction on disk".
  if (pathExists(journalPath(p_installDir))) {
    return recoverInterrupted(p_installDir);
  }

  QString error;
  const PendingPlan plan = readPending(p_installDir, &error);
  if (!plan.isValid()) {
    if (!pathExists(pendingPath(p_installDir))) {
      return makeApplyResult(ApplyStatus::NoPendingUpdate, QStringLiteral("no pending update"));
    }
    writeRetryableResult(p_installDir, QStringLiteral("pending update is invalid: %1").arg(error));
    return makeApplyResult(ApplyStatus::AbortedBeforeMutation, error);
  }

  TxContext ctx;
  ctx.installDir = QDir::cleanPath(QFileInfo(p_installDir).absoluteFilePath());
  ctx.stagedRoot = stagedDir(ctx.installDir);

  // ------------------------------------------------------------------
  // Preflight. Everything here runs BEFORE the first journaled mutation, so
  // any rejection leaves the install tree byte-for-byte untouched.
  // ------------------------------------------------------------------
  QString manifestError;
  const UpdateManifest target = UpdateManifest::fromJson(plan.targetManifest, &manifestError);
  if (!target.isValid()) {
    writeRetryableResult(p_installDir,
                         QStringLiteral("target manifest is invalid: %1").arg(manifestError));
    return makeApplyResult(ApplyStatus::AbortedBeforeMutation, manifestError, plan.targetVersion);
  }

  if (!isSameVolume(ctx.installDir, ctx.stagedRoot)) {
    writeRetryableResult(p_installDir,
                         QStringLiteral("staging directory is not on the install volume"));
    return makeApplyResult(ApplyStatus::AbortedBeforeMutation,
                           QStringLiteral("staging directory is not on the install volume"),
                           plan.targetVersion);
  }

  // Last line of defense before mutation: every staged file must still exist
  // and still match the target manifest.
  for (const QString &relative : plan.staged) {
    const QString staged = stagedPathOf(ctx, relative);
    if (!pathExists(staged)) {
      const QString reason = QStringLiteral("staged file '%1' is missing").arg(relative);
      writeRetryableResult(p_installDir, reason);
      return makeApplyResult(ApplyStatus::AbortedBeforeMutation, reason, plan.targetVersion);
    }
    UpdateManifestFile expected;
    if (!target.lookup(relative, &expected)) {
      const QString reason =
          QStringLiteral("staged file '%1' is not in the target manifest").arg(relative);
      writeRetryableResult(p_installDir, reason);
      return makeApplyResult(ApplyStatus::AbortedBeforeMutation, reason, plan.targetVersion);
    }
    if (QFileInfo(staged).size() != expected.size ||
        hashFile(staged).compare(expected.sha256, Qt::CaseInsensitive) != 0) {
      const QString reason = QStringLiteral("staged file '%1' no longer verifies").arg(relative);
      writeRetryableResult(p_installDir, reason);
      return makeApplyResult(ApplyStatus::AbortedBeforeMutation, reason, plan.targetVersion);
    }
  }

  // Path-type conflicts: a directory where a file must go (or vice versa).
  // Anything that cannot be moved aborts the plan here.
  QSet<QString> conflictKeys;
  QStringList conflicts;
  auto recordConflict = [&](const QString &p_relative) {
    const QString key = UpdateManifest::pathKey(p_relative);
    if (conflictKeys.contains(key)) {
      return;
    }
    conflictKeys.insert(key);
    conflicts.append(p_relative);
  };

  for (const QString &relative : plan.conflicts) {
    if (pathExists(targetPathOf(ctx, relative))) {
      recordConflict(relative);
    }
  }
  for (const QString &relative : plan.staged) {
    // The destination itself is a directory but a file must land there.
    const QString destination = targetPathOf(ctx, relative);
    if (pathExists(destination) && isDirectory(destination)) {
      recordConflict(relative);
    }
    // An ancestor is a file but must become a directory.
    const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QString acc;
    for (int i = 0; i + 1 < parts.size(); ++i) {
      if (!acc.isEmpty()) {
        acc += QLatin1Char('/');
      }
      acc += parts.at(i);
      const QString ancestor = targetPathOf(ctx, acc);
      if (pathExists(ancestor) && !isDirectory(ancestor)) {
        recordConflict(acc);
      }
    }
  }

  // ------------------------------------------------------------------
  // Build the operation list. The ReplaceExecutable op is forced LAST.
  // ------------------------------------------------------------------
  Journal journal;
  journal.phase = Journal::Phase::Applying;
  journal.targetVersion = plan.targetVersion;
  journal.executablePath = plan.executablePath;
  journal.targetManifest = plan.targetManifest;
  journal.backupDir = c_backupDirName + QLatin1Char('/') +
                      QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
  ctx.backupDirAbs = joinPath(ctx.installDir, journal.backupDir);

  for (const QString &relative : conflicts) {
    JournalOp op;
    op.type = JournalOp::Type::ConflictRemove;
    op.path = relative;
    journal.ops.append(op);
  }

  for (const QString &relative : plan.deletions) {
    if (conflictKeys.contains(UpdateManifest::pathKey(relative))) {
      // Already being moved away by a ConflictRemove op.
      continue;
    }
    JournalOp op;
    op.type = JournalOp::Type::Delete;
    op.path = relative;
    journal.ops.append(op);
  }

  const QString exeKey = UpdateManifest::pathKey(plan.executablePath);
  bool hasExecutableOp = false;
  for (const QString &relative : plan.staged) {
    if (UpdateManifest::pathKey(relative) == exeKey) {
      hasExecutableOp = true;
      continue;
    }
    JournalOp op;
    // A conflicting destination is removed by its ConflictRemove op, so treat
    // it as an Add here.
    const bool existsAsFile = pathExists(targetPathOf(ctx, relative)) &&
                              !isDirectory(targetPathOf(ctx, relative)) &&
                              !conflictKeys.contains(UpdateManifest::pathKey(relative));
    op.type = existsAsFile ? JournalOp::Type::Replace : JournalOp::Type::Add;
    op.path = relative;
    journal.ops.append(op);
  }

  if (hasExecutableOp) {
    JournalOp op;
    op.type = JournalOp::Type::ReplaceExecutable;
    op.path = plan.executablePath;
    op.expectedOldHash = hashFile(targetPathOf(ctx, plan.executablePath));
    op.expectedNewHash = hashFile(stagedPathOf(ctx, plan.executablePath));
    if (op.expectedOldHash.isEmpty() || op.expectedNewHash.isEmpty()) {
      const QString reason =
          QStringLiteral("cannot hash the current or staged executable '%1'").arg(op.path);
      writeRetryableResult(p_installDir, reason);
      return makeApplyResult(ApplyStatus::AbortedBeforeMutation, reason, plan.targetVersion);
    }
    journal.ops.append(op);
  }

  if (journal.ops.isEmpty()) {
    // Nothing to do; treat as an immediate commit so the manifest is refreshed.
    if (!QDir().mkpath(ctx.backupDirAbs)) {
      return makeApplyResult(ApplyStatus::AbortedBeforeMutation,
                             QStringLiteral("cannot create the backup directory"),
                             plan.targetVersion);
    }
    if (!saveJournal(p_installDir, journal)) {
      const QString reason = QStringLiteral("cannot write the update journal");
      writeRetryableResult(p_installDir, reason);
      return makeApplyResult(ApplyStatus::AbortedBeforeMutation, reason, plan.targetVersion);
    }
    return commitTransaction(p_installDir, journal, ctx);
  }

  // ------------------------------------------------------------------
  // First mutation starts here.
  // ------------------------------------------------------------------
  if (!QDir().mkpath(ctx.backupDirAbs)) {
    const QString reason = QStringLiteral("cannot create the backup directory");
    writeRetryableResult(p_installDir, reason);
    return makeApplyResult(ApplyStatus::AbortedBeforeMutation, reason, plan.targetVersion);
  }

  // RECOVERY.txt is the ONLY mitigation for residual risks 1 and 4 (a mixed
  // binary set, and the one-syscall window with no vnote.exe). If it cannot be
  // written there is no escape hatch, so refuse to start the transaction rather
  // than mutate the install tree without one.
  {
    const QStringList removeFirst = pathsToRemoveBeforeRestore(journal.ops);

    const QString recoveryPath = joinPath(ctx.backupDirAbs, c_recoveryFileName);
    const QByteArray text =
        recoveryText(ctx.installDir, journal.targetVersion, journal.backupDir, removeFirst)
            .toUtf8();

    QFile recovery(recoveryPath);
    const bool written = recovery.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                         recovery.write(text) == text.size() && recovery.flush();
    recovery.close();

    if (!written) {
      const QString reason =
          QStringLiteral("cannot write the manual-recovery instructions to '%1'").arg(recoveryPath);
      qCritical() << "VNote refusing to apply the update:" << reason;
      writeRetryableResult(p_installDir, reason);
      return makeApplyResult(ApplyStatus::AbortedBeforeMutation, reason, plan.targetVersion);
    }
  }

  if (!saveJournal(p_installDir, journal)) {
    const QString reason = QStringLiteral("cannot write the update journal");
    writeRetryableResult(p_installDir, reason);
    return makeApplyResult(ApplyStatus::AbortedBeforeMutation, reason, plan.targetVersion);
  }

  return runForward(p_installDir, journal, ctx);
}

// ===========================================================================
// recoverInterrupted
// ===========================================================================

UpdateInstaller::ApplyResult UpdateInstaller::recoverInterrupted(const QString &p_installDir) {
  Journal journal;
  if (!loadJournal(p_installDir, &journal)) {
    if (pathExists(journalPath(p_installDir))) {
      // A corrupt journal is the most dangerous case: we cannot know what was
      // done. Be conservative -- change nothing, preserve every artifact, and
      // surface it loudly.
      qCritical() << "VNote update journal is unreadable; leaving the install untouched:"
                  << journalPath(p_installDir);
      writeResult(p_installDir, ResultOutcome::ManualRecovery,
                  QStringLiteral("the update journal is corrupt"),
                  QStringLiteral("no changes were made; see RECOVERY.txt under .vnote-old"));
      return makeApplyResult(ApplyStatus::RollbackFailed,
                             QStringLiteral("update journal is corrupt"));
    }
    return makeApplyResult(ApplyStatus::NoPendingUpdate, QStringLiteral("no journal"));
  }

  TxContext ctx;
  ctx.installDir = QDir::cleanPath(QFileInfo(p_installDir).absoluteFilePath());
  ctx.stagedRoot = stagedDir(ctx.installDir);
  ctx.backupDirAbs = joinPath(ctx.installDir, journal.backupDir);

  if (journal.isTerminal()) {
    if (journal.phase == Journal::Phase::Committed) {
      // The commit was interrupted between "journal COMMITTED" and "staging
      // removed". finalizeCommit is idempotent.
      finalizeCommit(p_installDir, journal, ctx);
      return makeApplyResult(ApplyStatus::Committed, QStringLiteral("commit finalized"),
                             journal.targetVersion);
    }
    // ROLLED_BACK / MANUAL_RECOVERY: leave .vnote-update/ in place. A rolled
    // back transaction still has a usable staged tree, so the update can be
    // retried; a manual-recovery transaction must keep all its evidence.
    return makeApplyResult(journal.phase == Journal::Phase::RolledBack
                               ? ApplyStatus::RolledBack
                               : ApplyStatus::RollbackFailed,
                           QStringLiteral("journal already terminal"), journal.targetVersion);
  }

  // Reconcile the ReplaceExecutable op against the on-disk reality BEFORE
  // replaying anything, per the recovery state table in the plan (as amended by
  // the two-rename finding documented above execMoveAside). Its outcome can flip
  // the whole transaction from forward to reverse.
  //
  //   canonical | backup   | staged | phase        | action
  //   ----------+----------+--------+--------------+---------------------------
  //   old       | absent   | new    | APPLYING     | replay from move-aside
  //   absent    | == old   | new    | APPLYING     | replay the move-in
  //   absent    | == old   | bad    | APPLYING     | revert from backup
  //   new       | == old   | any    | APPLYING     | commit
  //   new       | == old   | any    | ROLLING_BACK | revert from backup
  //   old       | any      | any    | ROLLING_BACK | already reverted, finish
  //   absent    | == old   | any    | ROLLING_BACK | revert from backup
  //   new       | invalid  | any    | any          | commit only, log qCritical
  //   other     | == old   | any    | any          | restore from backup
  //   absent    | invalid  | any    | any          | UNRECOVERABLE
  //   other     | invalid  | any    | any          | UNRECOVERABLE
  for (int i = 0; i < journal.ops.size(); ++i) {
    JournalOp &op = journal.ops[i];
    if (op.type != JournalOp::Type::ReplaceExecutable) {
      continue;
    }

    const QString canonical = targetPathOf(ctx, op.path);
    const ExecClassification canonicalState =
        classifyCanonical(canonical, op.expectedOldHash, op.expectedNewHash);
    const bool canonicalPresent = pathExists(canonical);
    const bool backupOk = backupMatchesOld(ctx, op);
    const bool stagedOk = stagedMatchesNew(ctx, op);

    if (canonicalState == ExecClassification::CanonicalNew) {
      // Both renames completed.
      if (!backupOk) {
        qCritical() << "VNote executable was replaced but its backup is gone; the update can no "
                       "longer be reverted";
        op.state = JournalOp::State::Done;
        journal.phase = Journal::Phase::Applying;
        continue;
      }
      op.state = JournalOp::State::Done; // Reverse (if any) will undo it.
      continue;
    }

    if (canonicalState == ExecClassification::CanonicalOld) {
      // The move-aside never happened.
      if (journal.phase == Journal::Phase::RollingBack) {
        op.state = JournalOp::State::Reverted;
        continue;
      }
      if (!stagedOk) {
        // The staged image is gone; there is nothing to move in.
        journal.phase = Journal::Phase::RollingBack;
        op.state = JournalOp::State::Reverted;
        continue;
      }
      op.state = JournalOp::State::Intent;
      continue;
    }

    // Canonical is ABSENT (the one-syscall window between the two renames) or
    // holds something we do not recognize.
    if (backupOk) {
      if (!canonicalPresent && journal.phase == Journal::Phase::Applying && stagedOk) {
        // Resume forward: just move the new image in.
        op.state = JournalOp::State::BackedUp;
        continue;
      }

      if (!canonicalPresent) {
        // Revert: the backup goes straight back onto the vacant canonical name.
        journal.phase = Journal::Phase::RollingBack;
        op.state = JournalOp::State::BackedUp;
        continue;
      }

      // Canonical exists but matches neither hash: get rid of it and restore.
      QString error;
      if (removeRecursively(canonical) && executableRename(backupPathOf(ctx, op.path), canonical,
                                                           &error)) {
        op.state = JournalOp::State::Reverted;
        journal.phase = Journal::Phase::RollingBack;
        continue;
      }
      qCritical() << "VNote could not restore the executable from backup:" << error;
    }

    // Unrecoverable: preserve all evidence, never attempt a generic canonical
    // move, and ask for manual recovery.
    journal.phase = Journal::Phase::ManualRecovery;
    saveJournal(p_installDir, journal);
    qCritical() << "VNote executable is in an unrecoverable state; see RECOVERY.txt under"
                << ctx.backupDirAbs;
    writeResult(p_installDir, ResultOutcome::ManualRecovery,
                QStringLiteral("the VNote executable could not be recovered"),
                QStringLiteral("neither the installed nor the backup executable is usable"),
                journal.targetVersion);
    return makeApplyResult(ApplyStatus::RollbackFailed,
                           QStringLiteral("executable is unrecoverable"), journal.targetVersion);
  }

  if (!saveJournal(p_installDir, journal)) {
    // The reconciled ReplaceExecutable states decide forward-vs-reverse for the
    // WHOLE transaction. Replaying without them recorded risks re-running a
    // step that already happened.
    qCritical() << "VNote could not persist the reconciled recovery journal";
    writeResult(p_installDir, ResultOutcome::ManualRecovery,
                QStringLiteral("could not persist the recovery journal"),
                QStringLiteral("see RECOVERY.txt under .vnote-old"), journal.targetVersion);
    return makeApplyResult(ApplyStatus::RollbackFailed,
                           QStringLiteral("could not persist the recovery journal"),
                           journal.targetVersion);
  }

  if (journal.phase == Journal::Phase::RollingBack) {
    return rollback(p_installDir, journal, ctx,
                    QStringLiteral("resuming an interrupted rollback"));
  }

  return runForward(p_installDir, journal, ctx);
}

// ===========================================================================
// cleanupOldBackups
// ===========================================================================

void UpdateInstaller::cleanupOldBackups(const QString &p_installDir) {
  const QString root = backupRoot(p_installDir);
  if (!isDirectory(root)) {
    return;
  }

  // A non-terminal journal in .vnote-update/ means recovery still owns the
  // backups; never touch them.
  Journal live;
  if (loadJournal(p_installDir, &live) && !live.isTerminal()) {
    return;
  }

  const QFileInfoList entries =
      QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
  for (const QFileInfo &entry : entries) {
    // Each committed/rolled-back transaction leaves a copy of its terminal
    // journal beside the backups. A backup directory WITHOUT one belongs to a
    // transaction that never reached a terminal state, so it stays.
    Journal stored;
    QJsonObject obj;
    const QString journalCopy = joinPath(entry.absoluteFilePath(), c_journalFileName);
    if (!readJson(journalCopy, &obj) || !journalFromJson(obj, &stored) || !stored.isTerminal()) {
      continue;
    }
    QDir(entry.absoluteFilePath()).removeRecursively();
  }

  removeEmptyDir(root);
}

// ===========================================================================
// Test seams
// ===========================================================================

void UpdateInstaller::testSetFaultPoint(FaultPoint p_point, int p_opIndex) {
  g_faultPoint = p_point;
  g_faultOpIndex = p_opIndex;
}

void UpdateInstaller::testClearFaultPoint() {
  g_faultPoint = FaultPoint::None;
  g_faultOpIndex = -1;
}

void UpdateInstaller::testSetForcedRenameFailurePath(const QString &p_relativePath) {
  g_forcedRenameFailurePath = p_relativePath;
}

void UpdateInstaller::testSetForceAtomicRenameUnsupported(bool p_unsupported) {
  g_forceAtomicRenameUnsupported = p_unsupported;
}

void UpdateInstaller::testSetRetryBackoffEnabled(bool p_enabled) {
  g_retryBackoffEnabled = p_enabled;
}


