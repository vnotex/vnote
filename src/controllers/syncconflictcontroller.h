#ifndef SYNCCONFLICTCONTROLLER_H
#define SYNCCONFLICTCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include <core/noncopyable.h>

namespace vnotex {

class ServiceLocator;
class SyncService;

// SyncConflictController orchestrates the per-file conflict-resolution flow on
// behalf of the sync stack. It is the bridge between the view (T12's
// SyncConflictDialog2) and the service layer (T7's SyncService).
//
// Responsibilities:
//   * Spawn a SyncConflictDialog2 (non-blocking via QDialog::open()) for the
//     conflict file list reported by SyncService::conflictsDetected.
//   * On the dialog's resolutionsChosen signal, forward the per-file
//     resolutions to SyncService::resolveConflicts. resolveConflicts internally
//     queues a final triggerSync after all per-file resolutions, so the next
//     SyncService::syncFinished signal indicates the resolution pass is done.
//   * Re-emit the lifecycle events as conflictsResolved (after syncFinished)
//     and conflictsAbandoned (on dialog Cancel).
//
// Per ADR-1: this controller never includes sync/sync_manager.h. All vxcore
// sync interaction goes through SyncService.
//
// Per ADR-6: no method is virtual. Test seams (if any) are unconditional.
//
// Per the T12 ADR: Cancel does NOT auto-resolve any file. The dialog's
// rejected() signal triggers conflictsAbandoned and SyncService is NOT called.
//
// T16 will wire SyncService::conflictsDetected into presentConflicts() — this
// controller only exposes the public method; it does NOT subscribe to
// SyncService directly.
class SyncConflictController : public QObject, private Noncopyable {
  Q_OBJECT

public:
  explicit SyncConflictController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Open a SyncConflictDialog2 for @p_conflictFiles. The dialog is shown
  // non-blocking via QDialog::open() (NOT exec()) so SyncService progress
  // signals can continue to flow on the GUI thread while the user picks
  // resolutions. The dialog auto-deletes on close (Qt::WA_DeleteOnClose).
  //
  // On user OK, SyncService::resolveConflicts is invoked with the per-file
  // resolutions; conflictsResolved is emitted after the next syncFinished
  // arrives for @p_notebookId.
  //
  // On user Cancel, conflictsAbandoned is emitted; SyncService is NOT invoked.
  void presentConflicts(const QString &p_notebookId, const QStringList &p_conflictFiles,
                        QWidget *p_parent);

signals:
  // Emitted after the user-selected resolutions have been forwarded to
  // SyncService::resolveConflicts and the subsequent SyncService::syncFinished
  // has fired for the same notebookId.
  void conflictsResolved(const QString &p_notebookId);

  // Emitted when the user cancels the resolution dialog. SyncService is not
  // invoked; the underlying conflicts remain unresolved.
  void conflictsAbandoned(const QString &p_notebookId);

private:
  ServiceLocator &m_services;
  SyncService *m_syncService = nullptr;
};

} // namespace vnotex

#endif // SYNCCONFLICTCONTROLLER_H
