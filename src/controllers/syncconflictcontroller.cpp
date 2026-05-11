#include "syncconflictcontroller.h"

#include <memory>

#include <QDebug>
#include <QHash>
#include <QMetaObject>
#include <QString>

#include <core/servicelocator.h>
#include <core/services/synclog.h>
#include <core/services/syncservice.h>
#include <widgets/dialogs/syncconflictdialog2.h>

#include <vxcore/vxcore_types.h>

using namespace vnotex;

SyncConflictController::SyncConflictController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  m_syncService = m_services.get<SyncService>();
  // SyncService is required; controllers that depend on missing services would
  // hide bugs if they silently no-op. Q_ASSERT mirrors the convention in
  // SyncService's own constructor.
  Q_ASSERT(m_syncService);
}

void SyncConflictController::presentConflicts(const QString &p_notebookId,
                                              const QStringList &p_conflictFiles,
                                              QWidget *p_parent) {
  qCDebug(syncCategory) << "SyncConflictController::presentConflicts: notebookId:" << p_notebookId
                        << "conflictCount:" << p_conflictFiles.size();

  if (!m_syncService) {
    // Defensive: if SyncService is missing the abandon path keeps callers in
    // a consistent state.
    emit conflictsAbandoned(p_notebookId);
    return;
  }

  auto *dlg = new SyncConflictDialog2(m_services, p_notebookId, p_conflictFiles, p_parent);
  // Auto-clean: dialog destroys itself when closed (after either accept or
  // reject). The lambdas connected below run BEFORE the close (signal/slot is
  // direct), so they observe a still-valid dialog.
  dlg->setAttribute(Qt::WA_DeleteOnClose);

  // OK path: when the user clicks OK the dialog emits resolutionsChosen with
  // the per-file resolution map. We then:
  //   1. Wire a one-shot connection on SyncService::syncFinished filtered to
  //      this notebookId so that the very next syncFinished arrival emits
  //      conflictsResolved exactly once.
  //   2. Forward the resolutions to SyncService::resolveConflicts which
  //      internally queues per-file resolveConflict slots followed by a
  //      triggerSync; the trailing triggerSync is what produces syncFinished.
  // The shared_ptr<Connection> lets the lambda disconnect itself on its first
  // (and only) firing - the standard "one-shot" pattern from T7/T11/T14.
  connect(dlg, &SyncConflictDialog2::resolutionsChosen, this,
          [this, p_notebookId](const QHash<QString, QString> &p_resolutions) {
            auto conn = std::make_shared<QMetaObject::Connection>();
            *conn = connect(m_syncService, &SyncService::syncFinished, this,
                            [this, p_notebookId, conn](const QString &p_finishedNotebookId,
                                                       VxCoreError p_result) {
                              Q_UNUSED(p_result);
                              if (p_finishedNotebookId != p_notebookId) {
                                return;
                              }
                              QObject::disconnect(*conn);
                              emit conflictsResolved(p_notebookId);
                            });
            m_syncService->resolveConflicts(p_notebookId, p_resolutions);
          });

  // Cancel path: dialog Cancel emits QDialog::rejected. resolutionsChosen is
  // NOT emitted (per the T12 ADR), so the OK lambda above never fires; the
  // one-shot syncFinished connection is never installed. We just signal that
  // the user abandoned the dialog so callers (e.g., T16) can decide whether to
  // surface a status message.
  connect(dlg, &QDialog::rejected, this,
          [this, p_notebookId]() { emit conflictsAbandoned(p_notebookId); });

  // Non-blocking modal. exec() would spin a nested event loop and block any
  // queued SyncService progress signals from reaching the GUI thread, defeating
  // the responsiveness contract. open() shows the dialog as window-modal and
  // returns immediately, letting subsequent signals continue to flow.
  dlg->open();
}
