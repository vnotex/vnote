#ifndef OPENNOTEBOOKCONTROLLER_H
#define OPENNOTEBOOKCONTROLLER_H

#include <QMetaType>
#include <QObject>
#include <QString>

// Forward-declare the opaque C handle in the global namespace so member
// declarations below match the type used by vxcore's C API
// (vxcore_sync_create_cancellation / vxcore_sync_cancel /
// vxcore_sync_free_cancellation). Placing the forward decl inside vnotex
// would create a distinct unrelated type, breaking conversions at use sites.
struct VxCoreSyncCancellation_;

namespace vnotex {

class ServiceLocator;

// Input data structure for opening an existing notebook.
struct OpenNotebookInput {
  QString rootFolderPath;
  // T23: Forwarded to NotebookCoreService::openNotebookEx as the readOnly
  // option flag. Defaults to false so existing local-folder callers
  // (NotebookExplorer2::importNotebook) preserve their RW behavior without
  // change. Set true by the remote-clone path when the user supplies no PAT
  // (snapshot-only MVP per open-notebook-remote-readonly plan).
  bool readOnly = false;
};

// Result structure for notebook open operation.
struct OpenNotebookResult {
  bool success = false;
  QString notebookId;
  QString notebookName;
  QString errorMessage;
};

// Validation result structure for open notebook.
struct OpenNotebookValidationResult {
  bool valid = true;
  QString message;
};

// T22: Input for an end-to-end clone-then-open flow.
//
// The caller (typically OpenNotebookDialog2 via NotebookExplorer2) supplies
// the user's URL/PAT/destination choices; the controller owns staging-dir
// safety, async dispatch, and rollback semantics so the UI thread never
// blocks on libgit2 fetch/checkout.
//
// Field semantics:
//   * remoteUrl     : HTTPS or file:// URL (other schemes rejected at
//                     validate time). Required, non-empty.
//   * pat           : Personal access token. Empty means anonymous clone and
//                     the resulting notebook is opened read-only (no sync
//                     registration -- snapshot-only MVP).
//   * finalDestDir  : Absolute path of the user-chosen final destination
//                     folder. MUST NOT exist; the controller creates a
//                     sibling staging dir, clones into it, then renames
//                     into this path on success.
//   * backend       : Sync backend name (currently "git"; future-proofed).
//   * autoSyncEnabled: Boolean gate for auto-sync. Passed through to vxcore
//                     in the config JSON. Ignored for RO clones since they
//                     never register with SyncService.
struct CloneAndOpenInput {
  QString remoteUrl;
  QString pat;
  QString finalDestDir;
  QString backend = QStringLiteral("git");
  bool autoSyncEnabled = true;
};

// T22: Result of a clone-then-open operation.
//
// errorMessage is populated only when success == false. notebookName is the
// human-readable name from the cloned vx_notebook/config.json (empty if the
// clone failed before that point).
struct CloneAndOpenResult {
  bool success = false;
  QString notebookId;
  QString notebookName;
  QString errorMessage;
  // True when the notebook was opened in read-only mode (empty PAT path).
  bool isReadOnly = false;
  // True when the notebook was cloned from a remote URL with NO PAT: it lands
  // as a normal, fully-writable notebook with partial sync info persisted
  // (sync state S2 — syncEnabled=true, syncBackend="git", syncRemoteUrl=<url>,
  // no token). Such a clone must open SILENTLY (no PAT auto-prompt). Distinct
  // from isReadOnly, which is NOT used by the no-PAT path anymore.
  bool partialSyncNoPat = false;
};

// T22: Pre-flight validation result for CloneAndOpenInput. Mirrors the shape
// of OpenNotebookValidationResult so dialog code can share UX patterns.
struct CloneAndOpenValidationResult {
  bool valid = true;
  QString message;
};

// Controller for opening existing notebooks (local folder or remote URL).
//
// Local-folder path: validate root, open via NotebookCoreService::openNotebook.
// Remote-URL path (T22): validate URL/PAT/destination, generate a staging dir
// next to the final destination, clone into staging (worker thread), rename
// to final, optionally enable sync if PAT was supplied, fully rollback on
// failure. View (OpenNotebookDialog2) collects input and displays results;
// see src/widgets/dialogs/opennotebookdialog2.h. T25 wired the
// NotebookExplorer2 toolbar "Open Notebook" button to that dialog so the
// legacy QFileDialog::getExistingDirectory path is no longer reachable from
// the UI.
class OpenNotebookController : public QObject {
  Q_OBJECT

public:
  explicit OpenNotebookController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Validate root folder path for opening.
  // Checks: path exists, is a valid VNote notebook, not already open.
  OpenNotebookValidationResult validateRootFolder(const QString &p_path) const;

  // Open an existing notebook with the given input.
  // Returns result with success status and notebook ID or error message.
  // When p_input.readOnly is true the notebook opens read-only (T23).
  OpenNotebookResult openNotebook(const OpenNotebookInput &p_input);

  // T22: Validate clone inputs before any disk/network work. Mirrors the
  // dialog's inline checks in OpenNotebookDialog2::validateRemoteInputs so
  // both layers agree on what is acceptable. Checks (in order):
  //   * remoteUrl non-empty
  //   * remoteUrl matches https:// or file:/// scheme (SSH/HTTP/git
  //     rejected)
  //   * finalDestDir non-empty
  //   * parent of finalDestDir exists and is a writable directory
  //   * finalDestDir itself does NOT exist (we create it via the staging
  //     rename, see cloneAndOpen)
  //   * finalDestDir is not already an open notebook root (duplicate guard
  //     mirroring validateRootFolder)
  CloneAndOpenValidationResult validateCloneInput(const CloneAndOpenInput &p_input) const;

  // T22: Clone a remote notebook into p_input.finalDestDir and open it.
  // Asynchronous: returns immediately and emits cloneProgressUpdated /
  // cloneFinished signals when the worker completes. Use QueuedConnection
  // when subscribing from the GUI thread (which the dialog already does
  // via QObject::connect default).
  //
  // Flow per plan (open-notebook-remote-readonly T22):
  //   1. validateCloneInput; on fail emit cloneFinished(success=false).
  //   2. Generate staging dir via FileUtils2::generateCloneStagingDir.
  //   3. Spawn worker via QtConcurrent::run:
  //        a. Build config/credentials JSON.
  //        b. Call NotebookCoreService::cloneNotebookFromUrl into staging.
  //        c. On clone fail: removeStagingDir + emit failure.
  //        d. On clone OK: renameStagingToFinal. On rename fail: rollback
  //           (close notebook + delete finalDestDir).
  //        e. Re-open via openNotebookEx with the FINAL dir so vxcore
  //           refreshes its NotebookRecord.root_folder away from the
  //           now-renamed staging path (CRITICAL — without this vxcore
  //           remembers the obsolete staging path and the next session
  //           restore fails).
  //        f. If PAT non-empty: persist via SyncCredentialsStore +
  //           SyncService::enableSyncForNotebook. On failure: full
  //           rollback (delete keychain, close notebook, delete
  //           finalDestDir).
  //        g. If PAT empty: skip sync registration (RO snapshot).
  //        h. Emit cloneFinished(success=true, ..., isReadOnly=PAT empty).
  //
  // openurl-followups Item 2: cancellation. The controller now creates a
  // VxCoreSyncCancellation token at clone start, stores it in
  // m_currentCloneToken, and forwards it to
  // NotebookCoreService::cloneNotebookFromUrl. Callers may invoke
  // cancelClone() from the GUI thread to request an in-flight abort. The
  // token is freed on the GUI thread BEFORE cloneFinished is emitted so any
  // listener calling cancelClone() in response sees a nullptr (safe no-op).
  void cloneAndOpen(const CloneAndOpenInput &p_input);

  // openurl-followups Item 2: request cancellation of an in-flight clone.
  // Safe to call from the GUI thread at any time:
  //   - When no clone is in flight: no-op (m_currentCloneToken is null).
  //   - When a clone is in flight: flips the token's atomic cancel flag.
  //     The worker thread's libgit2 progress callback observes the flag on
  //     the next chunk and aborts with VXCORE_ERR_CANCELLED. The worker
  //     then runs the staging-dir cleanup rollback and emits cloneFinished
  //     with errorMessage set to a "cancelled by user" message.
  //
  // Lock-free per vxcore_sync_cancel's documented contract.
  void cancelClone();

signals:
  // T22: Coarse-grained progress hook for the dialog's status label. phase is
  // a short human-readable string ("Cloning...", "Registering sync...", etc.)
  // not intended for parsing. current/total are advisory; libgit2's clone is
  // largely opaque so callers typically use this for "alive" feedback.
  void cloneProgressUpdated(int current, int total, const QString &phase);

  // T22: Fires exactly once per cloneAndOpen call. Always emitted on the GUI
  // thread (the worker thread posts back via QMetaObject::invokeMethod with
  // QueuedConnection). On failure result.errorMessage is non-empty.
  void cloneFinished(const CloneAndOpenResult &p_result);

private:
  ServiceLocator &m_services;

  // openurl-followups Item 2: cancellation handle for an in-flight clone.
  //
  // Ownership / lifecycle (CRITICAL — read carefully before changing):
  //   * CREATED on the GUI thread inside cloneAndOpen, AFTER validation and
  //     BEFORE QtConcurrent::run spawns the worker. Allocation happens via
  //     vxcore_sync_create_cancellation (lock-free per vxcore docs).
  //   * READ from BOTH threads while the clone is in flight:
  //       - GUI thread: cancelClone() calls vxcore_sync_cancel(token). The
  //         token API is documented lock-free, so no extra mutex.
  //       - Worker thread: receives the same pointer through the closure
  //         capture and passes it into
  //         NotebookCoreService::cloneNotebookFromUrl, which forwards into
  //         vxcore_sync_clone_cancellable -> libgit2 progress callback.
  //   * FREED on the GUI thread inside the QMetaObject::invokeMethod
  //     callback that bounces back from the worker, BEFORE emitting
  //     cloneFinished. Setting m_currentCloneToken=nullptr before the
  //     emission guarantees any listener calling cancelClone() in response
  //     to cloneFinished sees a null pointer (safe no-op per vxcore's
  //     null-safety contract).
  //
  // Because the GUI thread is the SOLE thread that mutates this member
  // (initialization in cloneAndOpen and cleanup in the GUI-thread tail of
  // the worker), no synchronization beyond Qt's thread affinity is needed.
  // The worker thread reads its own captured copy of the raw pointer; the
  // member itself is only TOUCHED on the GUI thread.
  ::VxCoreSyncCancellation_ *m_currentCloneToken = nullptr;
};

} // namespace vnotex

// T22: register CloneAndOpenResult so the cloneFinished signal can be marshalled
// across the worker -> GUI thread queued connection.
Q_DECLARE_METATYPE(vnotex::CloneAndOpenResult)

#endif // OPENNOTEBOOKCONTROLLER_H
