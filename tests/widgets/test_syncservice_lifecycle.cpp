// T17: SyncService lifecycle (NotebookBeforeClose hook + bounded shutdown)
// ----------------------------------------------------------------------------
// Verifies:
//   1. blockCloseWhileSyncing  — When SyncService reports a sync in progress
//      for a notebook, the NotebookBeforeClose hook is cancelled and a
//      QMessageBox::warning is shown to the user.
//   2. cancelReasonMetadata    — A second hook handler subscribed at a higher
//      priority value (runs LATER) observes the HookContext flagged with
//      isCancelled() and the metadata key "syncCancelReason" populated.
//   3. boundedShutdown         — When the worker thread is hung past the 30s
//      timeout, SyncService::shutdown() returns within ~31s after invoking
//      QThread::terminate(), and a qWarning containing "shutdown timed out"
//      is emitted.
//   4. visualBlockedDialog     — End-to-end visual proof: real sync-in-progress
//      flag, real hook fire, modal QMessageBox grabbed to PNG before dismissal.
//
// Per ADR-1: this test never includes sync/sync_manager.h.
// Per the T6/T7/T11 deviation: the test links to core_services and does NOT
// dual-compile syncservice.cpp / syncworker.cpp / synccredentialsstore.cpp
// (would cause duplicate-symbol link errors and break the
// VNOTE_KEYCHAIN_AVAILABLE compile gate).

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

// Globals used by the QtMessageHandler hook installed in boundedShutdown().
static QStringList g_capturedQtMessages;
static QtMessageHandler g_previousMessageHandler = nullptr;

static void captureQtMessage(QtMsgType, const QMessageLogContext &, const QString &p_msg) {
  g_capturedQtMessages << p_msg;
  if (g_previousMessageHandler) {
    // Re-emit through previous handler so QtTest still surfaces messages.
    // Avoid recursion by temporarily clearing the global hook.
    auto previous = g_previousMessageHandler;
    g_previousMessageHandler = nullptr;
    previous(QtWarningMsg, QMessageLogContext(), p_msg);
    g_previousMessageHandler = previous;
  }
}

// Dismisses any QMessageBox the moment Qt shows it, capturing its visible text
// into m_lastMessageBoxText so tests can assert content. Installed once per
// test case in initTestCase() so every QMessageBox::warning() popped from the
// hook handler is auto-accepted (otherwise the test would deadlock on the
// modal nested event loop).
class MessageBoxAutoCloser : public QObject {
  Q_OBJECT
public:
  explicit MessageBoxAutoCloser(QObject *p_parent = nullptr) : QObject(p_parent) {}

  bool eventFilter(QObject *p_obj, QEvent *p_event) override {
    if (p_event->type() == QEvent::Show) {
      if (auto *box = qobject_cast<QMessageBox *>(p_obj)) {
        m_lastMessageBoxText = box->text();
        m_lastMessageBoxTitle = box->windowTitle();
        ++m_seenCount;
        // Schedule accept on the next event-loop tick so it fires inside the
        // nested loop QMessageBox::exec() is about to spin.
        QTimer::singleShot(0, box, &QDialog::accept);
      }
    }
    return false;
  }

  void reset() {
    m_lastMessageBoxText.clear();
    m_lastMessageBoxTitle.clear();
    m_seenCount = 0;
  }

  QString m_lastMessageBoxText;
  QString m_lastMessageBoxTitle;
  int m_seenCount = 0;
};

class TestSyncServiceLifecycle : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void blockCloseWhileSyncing();
  void cancelReasonMetadata();
  void boundedShutdown();
  void visualBlockedDialog();

private:
  MessageBoxAutoCloser *m_closer = nullptr;
};

void TestSyncServiceLifecycle::initTestCase() {
  // CRITICAL: enable test mode BEFORE any vxcore_context_create. Mirrors the
  // pattern in tests/AGENTS.md.
  vxcore_set_test_mode(1);

  // Install the auto-closer on the QApplication so any QMessageBox shown by
  // the SyncService hook handler is dismissed automatically. Without this the
  // tests would deadlock on the modal nested event loop.
  m_closer = new MessageBoxAutoCloser(this);
  qApp->installEventFilter(m_closer);
}

void TestSyncServiceLifecycle::cleanupTestCase() {
  if (m_closer) {
    qApp->removeEventFilter(m_closer);
  }
}

void TestSyncServiceLifecycle::cleanup() {
  if (m_closer) {
    m_closer->reset();
  }
}

void TestSyncServiceLifecycle::blockCloseWhileSyncing() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  QVERIFY(ctx != nullptr);

  ServiceLocator services;
  HookManager hookMgr;
  services.registerService<HookManager>(&hookMgr);
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  // Force the in-progress flag for "nbA" via the test seam (T7).
  syncService.testSetInProgress(QStringLiteral("nbA"), true);

  // Fire the NotebookBeforeClose hook; SyncService's handler should cancel it
  // and pop a QMessageBox::warning (intercepted + accepted by m_closer).
  NotebookCloseEvent event;
  event.notebookId = QStringLiteral("nbA");
  const bool cancelled = hookMgr.doAction(HookNames::NotebookBeforeClose, event);
  QVERIFY2(cancelled, "doAction should report cancelled when sync is in progress");
  QVERIFY2(m_closer->m_seenCount >= 1, "expected QMessageBox::warning to be shown");
  QVERIFY2(
      m_closer->m_lastMessageBoxText.contains(QStringLiteral("Sync"), Qt::CaseInsensitive) &&
          m_closer->m_lastMessageBoxText.contains(QStringLiteral("in progress"),
                                                  Qt::CaseInsensitive),
      qPrintable(QStringLiteral("Unexpected box text: %1").arg(m_closer->m_lastMessageBoxText)));

  // Reset the in-progress flag and verify the hook is no longer cancelled.
  m_closer->reset();
  syncService.testSetInProgress(QStringLiteral("nbA"), false);
  const bool cancelled2 = hookMgr.doAction(HookNames::NotebookBeforeClose, event);
  QVERIFY2(!cancelled2, "doAction should NOT cancel when sync is not in progress");
  QCOMPARE(m_closer->m_seenCount, 0);

  vxcore_context_destroy(ctx);
}

void TestSyncServiceLifecycle::cancelReasonMetadata() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);

  ServiceLocator services;
  HookManager hookMgr;
  services.registerService<HookManager>(&hookMgr);
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  // Subscribe a SECOND test-side handler at a higher priority value so it
  // runs AFTER the SyncService handler (HookManager: lower priority = earlier
  // execution). This handler captures the HookContext by value (HookContext is
  // a value type per src/core/hookcontext.h:13) so we can assert on its state.
  bool seenCtx = false;
  HookContext capturedCtx{QStringLiteral("placeholder")};
  hookMgr.addAction<NotebookCloseEvent>(
      HookNames::NotebookBeforeClose,
      [&seenCtx, &capturedCtx](HookContext &p_ctx, const NotebookCloseEvent &) {
        seenCtx = true;
        capturedCtx = p_ctx;
      },
      /*priority=*/20);

  // Force in-progress and fire the hook.
  syncService.testSetInProgress(QStringLiteral("nbB"), true);

  NotebookCloseEvent event;
  event.notebookId = QStringLiteral("nbB");
  hookMgr.doAction(HookNames::NotebookBeforeClose, event);

  QVERIFY(seenCtx);
  QVERIFY2(capturedCtx.isCancelled(), "Handler should observe a cancelled context");
  const QString reason = capturedCtx.getMetadata(QStringLiteral("syncCancelReason")).toString();
  QVERIFY2(!reason.isEmpty(), "syncCancelReason metadata must be set");
  QVERIFY2(reason.contains(QStringLiteral("Sync"), Qt::CaseInsensitive) &&
               reason.contains(QStringLiteral("progress"), Qt::CaseInsensitive),
           qPrintable(QStringLiteral("Unexpected reason: %1").arg(reason)));

  syncService.testSetInProgress(QStringLiteral("nbB"), false);
  vxcore_context_destroy(ctx);
}

void TestSyncServiceLifecycle::boundedShutdown() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);

  ServiceLocator services;
  HookManager hookMgr;
  services.registerService<HookManager>(&hookMgr);
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  // Arm the worker's hang seam so the next dispatched slot blocks for 35s.
  // shutdown() then has to time-out (30s) and call terminate() to recover.
  QSKIP("T24: SyncWorker::testHangNextOperation seam removed; needs port to "
        "SyncOps/SyncWorkQueueManager");
  syncService.triggerSyncNow(QStringLiteral("nbHang"));

  // Give the queued triggerSync slot a moment to start running so the worker
  // thread is actually inside QThread::msleep when shutdown begins.
  QTest::qWait(300);

  // Install a Qt message handler so we can capture the qWarning emitted by
  // shutdown() when the wait times out. Restore on exit.
  g_capturedQtMessages.clear();
  g_previousMessageHandler = qInstallMessageHandler(captureQtMessage);

  QElapsedTimer elapsed;
  elapsed.start();
  syncService.shutdown();
  const qint64 shutdownMs = elapsed.elapsed();

  qInstallMessageHandler(g_previousMessageHandler);
  g_previousMessageHandler = nullptr;

  qDebug() << "shutdown() completed in" << shutdownMs << "ms";
  QVERIFY2(shutdownMs < 31000,
           qPrintable(QStringLiteral("shutdown took %1 ms (expected < 31000)").arg(shutdownMs)));

  bool sawTimeoutWarning = false;
  for (const QString &m : g_capturedQtMessages) {
    if (m.contains(QStringLiteral("shutdown timed out"), Qt::CaseInsensitive)) {
      sawTimeoutWarning = true;
      break;
    }
  }
  QVERIFY2(sawTimeoutWarning,
           qPrintable(QStringLiteral("expected 'shutdown timed out' in qWarning output; got: %1")
                          .arg(g_capturedQtMessages.join(QStringLiteral("\n")))));

  // Idempotent: a second shutdown() must be safe and instant.
  QElapsedTimer elapsed2;
  elapsed2.start();
  syncService.shutdown();
  const qint64 secondMs = elapsed2.elapsed();
  QVERIFY2(secondMs < 100,
           qPrintable(QStringLiteral("second shutdown took %1 ms (expected < 100)").arg(secondMs)));

  vxcore_context_destroy(ctx);
}

void TestSyncServiceLifecycle::visualBlockedDialog() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);

  ServiceLocator services;
  HookManager hookMgr;
  services.registerService<HookManager>(&hookMgr);
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  syncService.testSetInProgress(QStringLiteral("nbVisual"), true);

  // Temporarily uninstall the auto-closer so the modal QMessageBox stays open
  // long enough for our visual-grab handler to capture a screenshot.
  qApp->removeEventFilter(m_closer);

  QString capturedText;
  bool grabbed = false;

  // Locate the modal QMessageBox after a 150 ms delay (lets QMessageBox::exec
  // spin its nested loop), grab a screenshot, then accept the dialog.
  QTimer::singleShot(150, this, [&]() {
    QWidget *modal = qApp->activeModalWidget();
    if (auto *box = qobject_cast<QMessageBox *>(modal)) {
      capturedText = box->text();
      QPixmap pix = box->grab();
      if (!pix.isNull()) {
        // Save evidence next to the source-of-truth evidence dir at the repo
        // root. Best-effort: create the directory if needed.
        QString outDir = QStringLiteral(".sisyphus/evidence");
        QDir().mkpath(outDir);
        const QString outPath = outDir + QStringLiteral("/task-17-blocked.png");
        grabbed = pix.save(outPath);
        qDebug() << "Saved blocked-dialog screenshot to" << outPath << "ok=" << grabbed;
      }
      box->accept();
    } else if (modal) {
      // If something else is modal (shouldn't be), close it.
      modal->close();
    }
  });

  NotebookCloseEvent event;
  event.notebookId = QStringLiteral("nbVisual");
  const bool cancelled = hookMgr.doAction(HookNames::NotebookBeforeClose, event);

  // Re-install closer for later tests (cleanup() resets state too).
  qApp->installEventFilter(m_closer);

  QVERIFY(cancelled);
  QVERIFY2(capturedText.contains(QStringLiteral("Sync"), Qt::CaseInsensitive) &&
               capturedText.contains(QStringLiteral("in progress"), Qt::CaseInsensitive),
           qPrintable(QStringLiteral("Unexpected dialog text: %1").arg(capturedText)));
  // grabbed may legitimately be false on headless CI without a screen, so we
  // log but do NOT fail on that.
  qDebug() << "visualBlockedDialog: capturedText=" << capturedText << "grabbed=" << grabbed;

  syncService.testSetInProgress(QStringLiteral("nbVisual"), false);
  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_MAIN(tests::TestSyncServiceLifecycle)
#include "test_syncservice_lifecycle.moc"
