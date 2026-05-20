// Tests for NotebookSyncInfoDialog2 B2 fix — wiring of the controller's
// error() signal to the dialog's onError slot.
//
// Subtests:
//   * error_signal_caught — emitting error() from the controller surfaces a
//     QMessageBox::critical (dismissed automatically by a timer).
//   * dialog_stays_open_on_error — after handling error(), the dialog has
//     NOT been accepted/rejected/closed.
//   * empty_error_renders_fallback — empty message string still shows a
//     dialog (uses generic fallback text) rather than being suppressed.

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

#include <controllers/notebooksyncinfocontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <temp_dir_fixture.h>
#include <widgets/dialogs/notebooksyncinfodialog2.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

namespace {

// Walk top-level widgets, click Ok on the first visible QMessageBox.
// Returns true if a box was dismissed. Used as a polling callback.
bool dismissAnyMessageBox() {
  const auto widgets = QApplication::topLevelWidgets();
  for (QWidget *w : widgets) {
    auto *box = qobject_cast<QMessageBox *>(w);
    if (box && box->isVisible()) {
      QAbstractButton *btn = box->button(QMessageBox::Ok);
      if (!btn) {
        // QMessageBox::critical with no explicit buttons gets a default Ok.
        const auto bs = box->buttons();
        if (!bs.isEmpty()) {
          bs.first()->click();
          return true;
        }
        box->reject();
        return true;
      }
      btn->click();
      return true;
    }
  }
  return false;
}

// Schedule a polling timer that dismisses the next visible QMessageBox.
// Sets *p_dismissed to true once a box was actually closed so tests can
// verify the modal was rendered (and not suppressed).
QTimer *scheduleDismissAndRecord(bool *p_dismissed) {
  auto *timer = new QTimer();
  timer->setInterval(50);
  int *tries = new int(0);
  QObject::connect(timer, &QTimer::timeout, [timer, tries, p_dismissed]() {
    if (dismissAnyMessageBox()) {
      *p_dismissed = true;
      timer->stop();
      delete tries;
      timer->deleteLater();
      return;
    }
    if (++*tries > 60) {
      timer->stop();
      delete tries;
      timer->deleteLater();
    }
  });
  timer->start();
  return timer;
}

} // namespace

class TestNotebookSyncInfoDialog2Error : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void error_signal_caught();
  void dialog_stays_open_on_error();
  void empty_error_renders_fallback();

private:
  // Build a fresh dialog with full service wiring. Returns the notebook id
  // via out-param so the caller can clean up credentials.
  NotebookSyncInfoDialog2 *makeDialog(VxCoreContextHandle ctx, ServiceLocator &services,
                                      NotebookCoreService &notebookService, TempDirFixture &tmp,
                                      const QString &nameSuffix, QString *p_outNbId);
};

void TestNotebookSyncInfoDialog2Error::initTestCase() { vxcore_set_test_mode(1); }

NotebookSyncInfoDialog2 *TestNotebookSyncInfoDialog2Error::makeDialog(
    VxCoreContextHandle ctx, ServiceLocator &services, NotebookCoreService &notebookService,
    TempDirFixture &tmp, const QString &nameSuffix, QString *p_outNbId) {
  Q_UNUSED(ctx);
  QString nbRoot = tmp.filePath(QStringLiteral("nb_b2_") + nameSuffix);
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"B2","syncEnabled":true,"syncBackend":"git"})", NotebookType::Bundled);
  if (nbId.isEmpty()) {
    return nullptr;
  }
  if (p_outNbId) {
    *p_outNbId = nbId;
  }
  auto *dialog = new NotebookSyncInfoDialog2(services, nbId);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  return dialog;
}

void TestNotebookSyncInfoDialog2Error::error_signal_caught() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString nbId;
  auto *dialog =
      makeDialog(ctx, services, notebookService, localTemp, QStringLiteral("caught"), &nbId);
  QVERIFY(dialog);

  auto *ctrl = dialog->findChild<NotebookSyncInfoController *>();
  QVERIFY(ctrl);

  bool dismissed = false;
  scheduleDismissAndRecord(&dismissed);

  // Emit error() from the controller.
  emit ctrl->error(QStringLiteral("simulated failure"));

  // Pump the loop so onError runs + the dismiss poller fires.
  for (int i = 0; i < 30 && !dismissed; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::qWait(50);
  }

  QVERIFY2(dismissed, "Error signal did not surface a QMessageBox");

  delete dialog;
  credStore.deleteCredentials(nbId);
  QTest::qWait(100);
  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoDialog2Error::dialog_stays_open_on_error() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString nbId;
  auto *dialog =
      makeDialog(ctx, services, notebookService, localTemp, QStringLiteral("stays_open"), &nbId);
  QVERIFY(dialog);

  auto *ctrl = dialog->findChild<NotebookSyncInfoController *>();
  QVERIFY(ctrl);

  QSignalSpy acceptedSpy(dialog, &QDialog::accepted);
  QSignalSpy rejectedSpy(dialog, &QDialog::rejected);
  QSignalSpy finishedSpy(dialog, &QDialog::finished);

  bool dismissed = false;
  scheduleDismissAndRecord(&dismissed);

  emit ctrl->error(QStringLiteral("simulated failure for stays_open"));

  for (int i = 0; i < 30 && !dismissed; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::qWait(50);
  }
  QVERIFY(dismissed);

  // Dialog MUST NOT have been closed by the error path.
  QCOMPARE(acceptedSpy.count(), 0);
  QCOMPARE(rejectedSpy.count(), 0);
  QCOMPARE(finishedSpy.count(), 0);

  delete dialog;
  credStore.deleteCredentials(nbId);
  QTest::qWait(100);
  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoDialog2Error::empty_error_renders_fallback() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  QString nbId;
  auto *dialog =
      makeDialog(ctx, services, notebookService, localTemp, QStringLiteral("empty"), &nbId);
  QVERIFY(dialog);

  auto *ctrl = dialog->findChild<NotebookSyncInfoController *>();
  QVERIFY(ctrl);

  bool dismissed = false;
  scheduleDismissAndRecord(&dismissed);

  // Empty message — MUST still render a dialog (generic fallback).
  emit ctrl->error(QString());

  for (int i = 0; i < 30 && !dismissed; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::qWait(50);
  }

  QVERIFY2(dismissed, "Empty error was suppressed silently — plan B2 violation");

  delete dialog;
  credStore.deleteCredentials(nbId);
  QTest::qWait(100);
  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_MAIN(tests::TestNotebookSyncInfoDialog2Error)
#include "test_notebooksyncinfodialog2_error.moc"
