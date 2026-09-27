// Tests for SyncConflictController (T13) — controller orchestrating
// SyncConflictDialog2 (T12) against SyncService (T7).
//
// Per ADR-1: this test never includes sync/sync_manager.h.
// Per the T12 ADR: Cancel does NOT auto-resolve any file; SyncService MUST NOT
// be invoked when the user clicks Cancel.
//
// Cases:
//   * okPathRoutesResolutions: presentConflicts() opens the dialog non-blocking;
//     clicking OK fires resolutionsChosen which the controller forwards to
//     SyncService::resolveConflicts. The next SyncService::syncFinished should
//     trigger conflictsResolved exactly once.
//   * cancelPath: clicking Cancel fires QDialog::rejected. The controller MUST
//     emit conflictsAbandoned and MUST NOT route to SyncService (no
//     syncFinished should fire).

#include <QApplication>
#include <QHash>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QWidget>
#include <QtTest>

#include <controllers/syncconflictcontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <widgets/dialogs/syncconflictdialog2.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestSyncConflictController : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();

  void failedResolutionKeepsIncidentActive();
  void cancelPath();
  void keepBothUsesBackendData();

private:
  // Walk top-level widgets and locate the (single) SyncConflictDialog2 spawned
  // by the controller. Returns nullptr if not found yet (caller may need to
  // process events first).
  static SyncConflictDialog2 *findOpenDialog();
};

void TestSyncConflictController::initTestCase() {
  // CRITICAL: enable test mode BEFORE any vxcore_context_create.
  vxcore_set_test_mode(1);
}

SyncConflictDialog2 *TestSyncConflictController::findOpenDialog() {
  // Filter on isVisible() so we ignore dialogs from a previous test that have
  // been hidden via accept()/reject() but whose deferred deletion (from
  // WA_DeleteOnClose) has not yet been processed. Without this filter the
  // previous test's stale dialog could be returned and clicks routed to the
  // wrong (already-closed) instance.
  for (QWidget *w : QApplication::topLevelWidgets()) {
    if (auto *d = qobject_cast<SyncConflictDialog2 *>(w)) {
      if (!d->isVisible()) {
        continue;
      }
      return d;
    }
  }
  return nullptr;
}

namespace {

// Drain pending deferred-delete events (e.g., from WA_DeleteOnClose) so dead
// dialogs don't show up in QApplication::topLevelWidgets() during the next
// test. Mirrors the cleanup pattern in tests/controllers/test_bootstrap.cpp.
void drainPendingEvents() {
  for (int i = 0; i < 20; ++i) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  }
}

} // namespace

void TestSyncConflictController::failedResolutionKeepsIncidentActive() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  {
    ServiceLocator services;
    NotebookCoreService notebookService(ctx);
    services.registerService<NotebookCoreService>(&notebookService);
    SyncCredentialsStore credStore(services);
    services.registerService<SyncCredentialsStore>(&credStore);
    SyncService syncService(services);
    services.registerService<SyncService>(&syncService);
    SyncConflictController controller(services);
    QSignalSpy resolved(&controller, &SyncConflictController::conflictsResolved);
    QSignalSpy finished(&syncService, &SyncService::syncFinished);
    controller.presentConflicts(QStringLiteral("no-longer-registered"), {QStringLiteral("note.md")},
                                nullptr);
    QTRY_VERIFY(findOpenDialog());
    auto *ok = findOpenDialog()->findChild<QPushButton *>(QStringLiteral("okButton"));
    QVERIFY(ok);
    ok->click();
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(qvariant_cast<VxCoreError>(finished.first().at(1)) != VXCORE_OK);
    QCOMPARE(resolved.count(), 0);
    syncService.shutdown();
    drainPendingEvents();
  }
  vxcore_context_destroy(ctx);
}

void TestSyncConflictController::keepBothUsesBackendData() {
  ServiceLocator services;
  const QStringList paths{QStringLiteral("vx_notebook/config.json"),
                          QStringLiteral("encrypted.vne"), QStringLiteral("special.txt"),
                          QStringLiteral("photo.bin"), QStringLiteral("ordinary.vne")};
  SyncConflictDialog2 dialog(services, QStringLiteral("notebook"), paths,
                             {paths.at(0), paths.at(1), paths.at(2)}, nullptr);
  QSignalSpy chosen(&dialog, &SyncConflictDialog2::resolutionsChosen);
  for (int i = 0; i < paths.size(); ++i) {
    auto *both = dialog.findChild<QRadioButton *>(QStringLiteral("radio_%1_both").arg(i));
    auto *remote = dialog.findChild<QRadioButton *>(QStringLiteral("radio_%1_remote").arg(i));
    QVERIFY(both);
    QVERIFY(remote);
    QCOMPARE(both->isEnabled(), i >= 3);
    if (i < 3) {
      both->click();
      QCOMPARE(dialog.resolutions().value(paths.at(i)), QStringLiteral("keep_local"));
      remote->click();
      QCOMPARE(dialog.resolutions().value(paths.at(i)), QStringLiteral("keep_remote"));
    } else {
      both->click();
      QCOMPARE(dialog.resolutions().value(paths.at(i)), QStringLiteral("keep_both"));
    }
  }
  auto *ok = dialog.findChild<QPushButton *>(QStringLiteral("okButton"));
  QVERIFY(ok);
  ok->click();
  QCOMPARE(chosen.count(), 1);
  SyncConflictDialog2 cancelled(services, QStringLiteral("notebook"), paths, {}, nullptr);
  QSignalSpy cancelledChoices(&cancelled, &SyncConflictDialog2::resolutionsChosen);
  auto *cancel = cancelled.findChild<QPushButton *>(QStringLiteral("cancelButton"));
  QVERIFY(cancel);
  cancel->click();
  QCOMPARE(cancelledChoices.count(), 0);
}

void TestSyncConflictController::cancelPath() {
  // Drain any leftover events from the previous test so a stale (hidden)
  // dialog from okPathRoutesResolutions cannot be observed here.
  drainPendingEvents();

  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  SyncConflictController controller(services);

  QSignalSpy abandonSpy(&controller, &SyncConflictController::conflictsAbandoned);
  QSignalSpy resolveSpy(&controller, &SyncConflictController::conflictsResolved);
  // Direct spy on SyncService::syncFinished to assert that NO sync was
  // triggered after Cancel (per the T12 ADR: Cancel does not call SyncService).
  QSignalSpy syncFinishedSpy(&syncService, &SyncService::syncFinished);

  const QString nbId = QStringLiteral("fake-notebook-id-cancel");
  const QStringList conflicts{QStringLiteral("a.md"), QStringLiteral("b.md")};

  controller.presentConflicts(nbId, conflicts, /*parent=*/nullptr);

  // Wait for the dialog to actually appear in topLevelWidgets.
  for (int i = 0; i < 50 && findOpenDialog() == nullptr; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QTest::qWait(20);
  }
  SyncConflictDialog2 *dlg = findOpenDialog();
  QVERIFY2(dlg != nullptr, "SyncConflictDialog2 was not opened by presentConflicts()");
  // Sanity: the dialog must NOT have already been finalized (result() == 0
  // means neither accept() nor reject() has fired). This guards against
  // accidentally picking up a stale dialog from a prior test whose deferred
  // delete hasn't run yet.
  QCOMPARE(dlg->result(), 0);

  QPushButton *cancelBtn = dlg->findChild<QPushButton *>(QStringLiteral("cancelButton"));
  QVERIFY2(cancelBtn != nullptr, "cancelButton not found on dialog");

  // Click Cancel via the QAbstractButton::click() API. QDialogButtonBox routes
  // a click on the Cancel-role button to QDialog::reject(), which emits
  // QDialog::rejected() synchronously. The controller's lambda (connected
  // directly to QDialog::rejected) emits conflictsAbandoned synchronously as
  // well, so by the time click() returns the signal is already in abandonSpy.
  //
  // We DO NOT call abandonSpy.wait() here: QSignalSpy::wait() returns false
  // when the signal has already arrived BEFORE wait() is called (it waits for
  // a NEW emission). So we just process any pending events and assert the
  // count directly.
  cancelBtn->click();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

  QCOMPARE(abandonSpy.count(), 1);
  QCOMPARE(abandonSpy.first().at(0).toString(), nbId);

  // Per the ADR: Cancel must not auto-resolve any file. SyncService must not
  // have been invoked, so no syncFinished should fire and no conflictsResolved
  // either. Give the worker thread a small window to make any spurious
  // signals visible before we assert zero.
  for (int i = 0; i < 10; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QTest::qWait(20);
  }
  QCOMPARE(syncFinishedSpy.count(), 0);
  QCOMPARE(resolveSpy.count(), 0);

  // Drain tail events.
  drainPendingEvents();

  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_MAIN(tests::TestSyncConflictController)
#include "test_syncconflictcontroller.moc"
