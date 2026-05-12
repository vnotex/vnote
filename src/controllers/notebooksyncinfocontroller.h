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
//     and lastSyncIso for display in the dialog.
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

  // Read the FLAT ADR-8 "lastSyncIso" value from the notebook config. Empty if
  // the key is absent (e.g., the notebook has never been synced).
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

signals:
  void dataLoaded(const QString &p_notebookName, const QString &p_remoteUrl,
                  const QString &p_lastSyncTime);
  void applyComplete(bool p_success);
  void disableComplete(const QString &p_notebookId);
  void error(const QString &p_message);

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
};

} // namespace vnotex

#endif // NOTEBOOKSYNCINFOCONTROLLER_H
