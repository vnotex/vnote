#ifndef OPENNOTEBOOKCONTROLLER_H
#define OPENNOTEBOOKCONTROLLER_H

#include <QMetaType>
#include <QObject>
#include <QString>

#include <core/services/syncsettings.h>

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
  // Explicit local-folder read-only option; remote downloads remain writable.
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

// The controller downloads into an owned sibling staging directory, then renames
// to a destination that MUST NOT already exist. Empty credentials request an
// anonymous, writable download with partial sync settings (S2).
struct CloneAndOpenInput {
  SyncSettings syncSettings;
  QString finalDestDir;
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
  // Remote downloads are writable, including anonymous partial-sync downloads.
  bool isReadOnly = false;
  // True for a silent S2 open without saved credentials or runtime registration.
  bool partialSyncMissingCredentials = false;
};

// T22: Pre-flight validation result for CloneAndOpenInput. Mirrors the shape
// of OpenNotebookValidationResult so dialog code can share UX patterns.
struct CloneAndOpenValidationResult {
  bool valid = true;
  QString message;
};

// Controller for opening existing notebooks (local folder or remote URL).
//
// Remote downloads use shared settings validation and never write remotely.
// Authenticated downloads enable sync only after final rename and reopen.
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

  // Shared URL/backend validation; destination must not exist, including an
  // empty directory. Parent must be an existing writable directory.
  CloneAndOpenValidationResult validateCloneInput(const CloneAndOpenInput &p_input) const;

  // T22: Clone a remote notebook into p_input.finalDestDir and open it.
  // Asynchronous: returns immediately and emits cloneProgressUpdated /
  // cloneFinished signals when the worker completes. Use QueuedConnection
  // when subscribing from the GUI thread (which the dialog already does
  // via QObject::connect default).
  //
  // Clone into owned staging, rename, close/reopen against the final root, then
  // register authenticated sync or persist anonymous S2 settings. Failure removes
  // only staging/the final root created by this operation, never an existing path.
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
