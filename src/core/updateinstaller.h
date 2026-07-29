#ifndef UPDATEINSTALLER_H
#define UPDATEINSTALLER_H

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace vnotex {

class UpdateManifest;

// Applies a staged incremental update to the install tree, and recovers from an
// interrupted apply.
//
// This class runs AFTER every service, ConfigMgr2, Application and the
// SingleInstanceGuard have been destroyed (see the post-scope block in
// src/main.cpp), and BEFORE Qt exists at all on the recovery path (it is called
// from the first statements of main()). It therefore MUST NOT touch:
//
//   qApp, widgets, translations, queued signals, QtConcurrent/QFutureWatcher,
//   the network, or any pointer into ServiceLocator / ConfigMgr2 /
//   UpdateService / Application.
//
// Only synchronous QtCore (QFile, QSaveFile, QDir, QCryptographicHash, JSON) and
// Win32 are allowed. The plan is re-read from pending.json.
//
// Everything is static; there is no instance state to leak across the swap.
class UpdateInstaller {
public:
  // ---------------------------------------------------------------------
  // Layout
  // ---------------------------------------------------------------------
  //   <installDir>/.vnote-update/download/     downloaded archives
  //   <installDir>/.vnote-update/staged/       extracted new files
  //   <installDir>/.vnote-update/pending.json  the plan
  //   <installDir>/.vnote-update/journal.json  durable write-ahead journal
  //   <installDir>/.vnote-update/result.json   apply outcome, read at next launch
  //   <installDir>/.vnote-old/<timestamp>/     backups + RECOVERY.txt
  //
  // The lease sentinel deliberately lives OUTSIDE .vnote-update/ (it is
  // <installDir>/.vnote-update.lease) so committing can delete the whole
  // directory without a lock-vs-directory ordering race.
  static QString stagingRoot(const QString &p_installDir);
  static QString downloadDir(const QString &p_installDir);
  static QString stagedDir(const QString &p_installDir);
  static QString pendingPath(const QString &p_installDir);
  static QString journalPath(const QString &p_installDir);
  static QString resultPath(const QString &p_installDir);
  static QString backupRoot(const QString &p_installDir);

  // ---------------------------------------------------------------------
  // Module-path helpers (no QCoreApplication required)
  // ---------------------------------------------------------------------
  static QString exePathFromModulePath();
  static QString installDirFromModulePath();

  // ---------------------------------------------------------------------
  // Eligibility probes
  // ---------------------------------------------------------------------

  // Creates and deletes <installDir>/.vnote-write-test.
  static bool isInstallDirWritable(const QString &p_installDir);

  // %ProgramFiles% / %ProgramFiles(x86)% heuristic: an MSI install lives there
  // and must never be self-updated in place.
  static bool isUnderProgramFiles(const QString &p_installDir);

  // Both paths must resolve to the same volume for the atomic rename (and for
  // every ordinary rename) to work.
  static bool isSameVolume(const QString &p_pathA, const QString &p_pathB);

  // Runtime capability probe for the EXECUTABLE swap sequence.
  //
  // IMPORTANT (empirical, see the table above execMoveAside in the .cpp): a
  // running executable is mapped as an IMAGE section, and Windows refuses to
  // unlink the name of such a file -- `SetFileInformationByHandle(FileRenameInfoEx,
  // FILE_RENAME_POSIX_SEMANTICS | FILE_RENAME_REPLACE_IF_EXISTS)` ONTO it fails
  // with ERROR_ACCESS_DENIED. Renaming the mapped image ASIDE is allowed. The
  // swap is therefore two renames, and this probe exercises exactly that
  // sequence against a real SEC_IMAGE-mapped PE:
  //
  //   1. copy a valid PE into <installDir>/.vnote-update/ and keep a live
  //      SEC_IMAGE mapping of it (so it behaves like a running executable);
  //   2. rename the mapped image aside;
  //   3. rename a throwaway file into the vacated name.
  //
  // Detection is by ACTUAL API RESULT, never by OS version or build variant:
  // driver and filesystem support also matter, and a Qt5 "-windows7" build on
  // Windows 11 must still qualify.
  //
  // Called at planning time AND re-run in apply preflight: a pending update can
  // outlive an OS, driver or filesystem change.
  static bool probeAtomicRenameSupport(const QString &p_installDir);

  // ---------------------------------------------------------------------
  // WebEngine child reaping
  // ---------------------------------------------------------------------
  enum class WaitResult {
    // No descendant processes at all.
    NoChildren,
    // Descendants existed and all exited within the timeout.
    Exited,
    // Descendants were still alive when the timeout expired.
    TimedOut,
    // A candidate could not be opened or enumerated. NOT "no child".
    Error,
  };

  // Waits for THIS process's descendants whose image lives inside the install
  // directory (QtWebEngineProcess.exe and friends) to exit.
  //
  // Never terminates a process. TimedOut and Error abort the apply BEFORE the
  // first journaled operation.
  static WaitResult waitForWebEngineChildren(const QString &p_installDir, int p_timeoutMs);

  static QString waitResultToString(WaitResult p_result);

  // ---------------------------------------------------------------------
  // Apply
  // ---------------------------------------------------------------------
  enum class ApplyStatus {
    // Transaction committed; the tree now matches the target manifest.
    Committed,
    // Nothing to do (no pending.json).
    NoPendingUpdate,
    // The plan was rejected before any mutation. Nothing changed.
    AbortedBeforeMutation,
    // A journaled operation failed and the tree was fully restored.
    RolledBack,
    // A journaled operation failed AND rollback was also blocked. The install
    // is mixed; journal + backups are intact and RECOVERY.txt explains manual
    // recovery. This is residual risk 2, tier 3.
    RollbackFailed,
    // A test fault point fired. Models a process kill: no rollback is attempted
    // and the journal is deliberately left mid-transaction.
    FaultInjected,
  };

  struct ApplyResult {
    ApplyStatus status = ApplyStatus::NoPendingUpdate;
    QString message;
    QString targetVersion;

    bool isCommitted() const { return status == ApplyStatus::Committed; }
  };

  // Reads pending.json and performs the swap under a durable journal.
  //
  // The caller MUST already hold the UpdateLease and MUST have re-run
  // probeAtomicRenameSupport() and waitForWebEngineChildren() first.
  static ApplyResult applyPending(const QString &p_installDir);

  // ---------------------------------------------------------------------
  // Recovery / cleanup
  // ---------------------------------------------------------------------

  // Idempotently completes or reverses a non-terminal journal. Safe to call
  // repeatedly, including after a crash DURING recovery. Runs as one of the
  // first statements of main(), under the startup lease.
  static ApplyResult recoverInterrupted(const QString &p_installDir);

  // Deletes .vnote-old/<ts>/ trees whose transaction reached a terminal state.
  // Best effort: leftovers are retried on a later launch.
  static void cleanupOldBackups(const QString &p_installDir);

  // ---------------------------------------------------------------------
  // result.json
  // ---------------------------------------------------------------------
  enum class ResultOutcome {
    None,
    Applied,
    // Something transient blocked the apply; the pending update is preserved
    // and will be retried on the next quit.
    Retryable,
    // The apply failed and was rolled back.
    Failed,
    // Mixed install; a human has to look at RECOVERY.txt.
    ManualRecovery,
    // The apply finished but the replacement process could not be spawned.
    SpawnFailed,
  };

  struct StoredResult {
    ResultOutcome outcome = ResultOutcome::None;
    QString reason;
    QString detail;
    QString targetVersion;
    QString timestamp;

    bool isValid() const { return outcome != ResultOutcome::None; }
  };

  static bool writeResult(const QString &p_installDir, ResultOutcome p_outcome,
                          const QString &p_reason, const QString &p_detail = QString(),
                          const QString &p_targetVersion = QString());

  // Convenience wrappers used by main.cpp.
  static bool writeRetryableResult(const QString &p_installDir, const QString &p_reason);
  static bool writeSpawnFailure(const QString &p_installDir);

  static StoredResult readResult(const QString &p_installDir);
  static bool clearResult(const QString &p_installDir);

  // ---------------------------------------------------------------------
  // Hashing
  // ---------------------------------------------------------------------

  // Lowercase hex SHA-256, or an empty string when the file cannot be read.
  static QString hashFile(const QString &p_path);

  // ---------------------------------------------------------------------
  // Plan construction / persistence (used by UpdateService and by tests)
  // ---------------------------------------------------------------------
  struct PendingPlan {
    static constexpr int c_schema = 1;

    QString targetVersion;
    QString variant;

    // Install-root-relative path of the executable that gets the atomic
    // ReplaceExecutable treatment. Defaults to the running module's file name.
    QString executablePath;

    // Normalized paths present under staged/ that must be moved into place.
    QStringList staged;

    // Normalized paths to remove from the install tree.
    QStringList deletions;

    // Normalized paths whose CURRENT on-disk type conflicts with the required
    // type (a directory where a file must go, or vice versa). Backed up and
    // removed before the corresponding staged file is moved in.
    QStringList conflicts;

    // Full target manifest, committed as manifest.json at the end.
    QJsonObject targetManifest;

    bool isValid() const;

    QJsonObject toJson() const;
    static PendingPlan fromJson(const QJsonObject &p_obj, QString *p_error = nullptr);
  };

  static bool writePending(const QString &p_installDir, const PendingPlan &p_plan);
  static PendingPlan readPending(const QString &p_installDir, QString *p_error = nullptr);
  static bool clearPending(const QString &p_installDir);

  // Removes the whole .vnote-update/ directory. Safe to call when it is absent.
  static bool removeStagingRoot(const QString &p_installDir);

  // ---------------------------------------------------------------------
  // Test seams (unconditional, per ADR-6)
  // ---------------------------------------------------------------------

  // Deterministic kill points. When armed, applyPending()/recoverInterrupted()
  // return FaultInjected at exactly that point WITHOUT rolling back, modelling a
  // process kill. Tests then re-run recoverInterrupted() and assert convergence.
  enum class FaultPoint {
    None,
    // Ordinary operations.
    AfterIntentCommit,
    AfterBackupSyscall,
    AfterStagedToTargetMove,
    AfterDoneCommit,
    // Rollback.
    RollbackAfterPhaseCommit,
    // Fires INSIDE performReverse, AFTER the backup has been restored onto the
    // target but BEFORE `Reverted` is journaled. This is the dangerous replay
    // window: the journal still says `Done` while the restore has already
    // happened, so a naive replay would move the freshly restored ORIGINAL into
    // staging and then find no backup left to put back.
    RollbackAfterBackupRestored,
    RollbackAfterRestore,
    // Manifest.
    BeforeManifestCommit,
    AfterManifestCommit,
    // ReplaceExecutable state machine (two renames -- see probeAtomicRenameSupport).
    ExecAfterIntentCommit,
    // Fires while the canonical executable name DOES NOT EXIST: the running
    // image has been moved into the backup and the new one is not in yet. The
    // nastiest reachable state, and the one recovery must converge from.
    ExecAfterMoveAside,
    ExecAfterMoveIn,
  };

  // p_opIndex < 0 fires at the first operation that reaches the point.
  static void testSetFaultPoint(FaultPoint p_point, int p_opIndex = -1);
  static void testClearFaultPoint();

  // Forces every rename whose DESTINATION matches this install-root-relative
  // path to fail, simulating a handle held by an AV scanner. Empty clears it.
  static void testSetForcedRenameFailurePath(const QString &p_relativePath);

  // Forces the atomic-rename capability probe to report failure.
  static void testSetForceAtomicRenameUnsupported(bool p_unsupported);

  // Disables the retry backoff so failure tests do not sleep for seconds.
  static void testSetRetryBackoffEnabled(bool p_enabled);

private:
  UpdateInstaller() = delete;
};

} // namespace vnotex

#endif // UPDATEINSTALLER_H
