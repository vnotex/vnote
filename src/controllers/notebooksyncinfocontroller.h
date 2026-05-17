#ifndef NOTEBOOKSYNCINFOCONTROLLER_H
#define NOTEBOOKSYNCINFOCONTROLLER_H

#include <QObject>
#include <QString>

#include <core/noncopyable.h>

#include <vxcore/vxcore_types.h>

namespace vnotex {

class ServiceLocator;

// NotebookSyncInfoController orchestrates the read/edit/disable flows for the
// per-notebook git sync configuration on behalf of NotebookSyncInfoDialog2.
//
// Responsibilities:
//   * Load the notebook's display name, current syncRemoteUrl (flat ADR-8 key),
//     and last-sync timestamp for display in the dialog.
//   * Persist URL changes by mutating the notebook config JSON via
//     NotebookCoreService::updateNotebookConfig. Per ADR-8 the key is the FLAT
//     "syncRemoteUrl" (NOT a nested "sync.remoteUrl").
//   * Forward PAT updates to SyncService::updateCredentials, which in turn
//     routes to SyncCredentialsStore. PAT values are NEVER cached on this
//     controller (per ADR-9).
//   * Disable sync via SyncService::disableSyncForNotebook; the credentials
//     store entry is deleted as part of T7's disableFinished follow-up.
//
// Per ADR-1: this controller never includes sync/sync_manager.h directly.
// Per ADR-6: no method is virtual. Test seams (if any) are unconditional.
class NotebookSyncInfoController : public QObject, private Noncopyable {
  Q_OBJECT

public:
  explicit NotebookSyncInfoController(ServiceLocator &p_services, const QString &p_notebookId,
                                      QObject *p_parent = nullptr);

  // Read the notebook's display name from NotebookCoreService::getNotebookConfig.
  QString notebookName() const;

  // Read the FLAT ADR-8 "syncRemoteUrl" value from the notebook config. Empty
  // if the key is absent.
  QString remoteUrl() const;

  // Read the per-device last-sync timestamp via NotebookCoreService and format
  // it as a locale-aware short string. Empty if the notebook has never been
  // synced on this device (millis == 0).
  QString lastSyncTime() const;

  // Populate internal state and emit dataLoaded(...) so the dialog can refresh
  // its read-only labels and snapshot the last-applied URL.
  void loadInitialData();

  // Apply the user's edits. URL changes are persisted via
  // NotebookCoreService::updateNotebookConfig; a non-empty new PAT is forwarded
  // to SyncService::updateCredentials. Emits applyComplete(success) when all
  // queued operations have completed (success is true iff every step
  // succeeded).
  void applyChanges(const QString &p_newRemoteUrl, const QString &p_newPat);

  // Ask SyncService to disable sync for this notebook. The matching keychain
  // entry is removed by T7 in response to disableFinished. This controller
  // re-emits disableComplete(notebookId) once disableFinished arrives for
  // m_notebookId.
  void disableSync();

  // W3.T1 — Atomically enable sync on an EXISTING partial notebook
  // (S1/S2/S3/S4 -> S5). Mirrors NewNotebookController::bootstrapSync but
  // does NOT delete the notebook on failure (the notebook already exists on
  // disk with user content; the partial sync config is intentionally left in
  // place so the user can retry without losing their notebook).
  //
  // Sequence:
  //   1. Install a one-shot connection on SyncService::enableFinished filtered
  //      by m_notebookId.
  //   2. Call SyncService::enableSyncForNotebook(m_notebookId, p_url, p_pat).
  //   3. On VXCORE_OK: persist syncRemoteUrl via persistRemoteUrl(), emit
  //      applyComplete(true), trigger an initial sync via
  //      SyncService::triggerSyncNow.
  //   4. On failure: emit applyComplete(false) + error(message). KEEP the
  //      notebook intact (no closeNotebook / removeRecursively, unlike
  //      NewNotebookController::bootstrapSync).
  //   5. Always disconnect the one-shot connection after firing.
  //
  // The PAT is forwarded to SyncService and NEVER cached on this controller
  // (per ADR-9). It is also NEVER logged in any branch.
  void bootstrapApply(const QString &p_url, const QString &p_pat);

public slots:
  // W3.T3 — Confirmation callback for URL change on a registered (S5)
  // notebook. applyChanges() emits confirmUrlChangeRequested() when the user
  // tries to change the URL on a sync-registered notebook; the dialog shows a
  // QMessageBox and routes the user's decision back here.
  //
  // @p_confirmed: true if the user accepted the destructive URL change,
  //   false if cancelled.
  //
  // On confirm: reads the existing PAT from the keychain (if the user did
  // NOT provide a new PAT in the Sync Info dialog), then chains
  // disableSyncForNotebook -> on disableFinished VXCORE_OK ->
  // re-enable with the new URL + PAT. On re-enable success, restores the
  // notebook config's syncEnabled / syncBackend / syncRemoteUrl fields and
  // triggers an initial sync. On re-enable failure, emits error() +
  // applyComplete(false) and leaves the notebook in disabled state (clean
  // S0 via W2.T5 cleanup) so the user can re-enable via the Sync Info UI.
  //
  // On cancel: clears pending state and returns (no-op). The dialog is
  // responsible for resetting its URL field.
  //
  // Idempotent: if no URL change is pending, this is a no-op.
  void confirmUrlChange(bool p_confirmed);

signals:
  void dataLoaded(const QString &p_notebookName, const QString &p_remoteUrl,
                  const QString &p_lastSyncTime);
  void applyComplete(bool p_success);
  void disableComplete(const QString &p_notebookId);
  void error(const QString &p_message);

  // W3.T3 — Emitted by applyChanges() when the user tries to change the URL
  // on a sync-registered (S5) notebook. The dialog should show a QMessageBox
  // confirming the destructive action (disable + wipe + re-enable) and then
  // call confirmUrlChange(true/false) to continue or abort.
  void confirmUrlChangeRequested(const QString &p_oldUrl, const QString &p_newUrl);

private slots:
  // Bridge SyncService::credentialsSetFinished into applyComplete when we are
  // currently waiting on a PAT update. Filters by m_notebookId.
  void onCredentialsSetFinished(const QString &p_notebookId, VxCoreError p_result);

  // Bridge SyncService::disableFinished into disableComplete. Filters by
  // m_notebookId.
  void onDisableFinished(const QString &p_notebookId, VxCoreError p_result);

private:
  // Persist a new syncRemoteUrl by reading the existing JSON, mutating the
  // flat ADR-8 key, and writing it back. Returns true on success.
  bool persistRemoteUrl(const QString &p_newRemoteUrl);

  // W3.T3 — Internal helper for the URL-change-on-S5 atomic flow. Chains
  // disableSyncForNotebook -> re-enable with new URL/PAT. Called after the
  // user confirms via confirmUrlChange(true) and (if needed) after the
  // existing PAT has been fetched from the keychain.
  void performAtomicUrlReChange(const QString &p_newUrl, const QString &p_pat);

  ServiceLocator &m_services;
  QString m_notebookId;

  // Cache of the URL last loaded from the notebook config; used by
  // applyChanges() to detect whether a write is necessary.
  QString m_currentRemoteUrl;

  // True while waiting on a PAT update to complete via
  // SyncService::credentialsSetFinished. Used by onCredentialsSetFinished to
  // know when to emit applyComplete (and whether to propagate the result).
  bool m_pendingPatUpdate = false;

  // Snapshot of the URL-write outcome for the in-flight applyChanges; combined
  // with the PAT result when both finish to emit applyComplete(success).
  bool m_pendingUrlWriteOk = true;

  // W3.T3 — Pending URL-change-confirmation state. Set by applyChanges()
  // when it detects a URL change on a sync-registered notebook; consumed by
  // confirmUrlChange(). The PAT field stores the user-provided NEW PAT (if
  // any); when empty, confirmUrlChange() will fetch the existing PAT from
  // the keychain BEFORE disable (since disable+W2.T5 wipes the keychain).
  bool m_pendingUrlChange = false;
  QString m_pendingUrlChangeNewUrl;
  QString m_pendingUrlChangeProvidedPat;
};

} // namespace vnotex

#endif // NOTEBOOKSYNCINFOCONTROLLER_H
