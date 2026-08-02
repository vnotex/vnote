// NotificationRouter: subsystem failure signals -> NotificationMessages.
//
// GUILESS. The router holds no widget pointers by design, so the widget-owned
// sources (NotebookExplorer2, ViewArea2) are driven through its public slots
// without constructing any widget.

#include <QtTest>

#include <QSignalSpy>

#include <controllers/notificationrouter.h>
#include <core/servicelocator.h>
#include <core/services/imagehostservice.h>
#include <core/services/notificationservice.h>
#include <imagehost/imagehosttypes.h>

using namespace vnotex;

namespace tests {

class TestNotificationRouter : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void test_syncAuthFailureIsInterruptingAndKeyed();
  void test_syncNetworkFailureIsPassive();
  void test_syncOtherFailureUsesTheGenericKey();
  void test_repeatSyncFailureFoldsAndDoesNotInterrupt();
  void test_manualRetryRetiresTheIncidentSoTheNextFailureInterrupts();
  void test_notebookSwitchDoesNotRetireAnotherNotebook();
  void test_syncAuthActionEmitsOpenSyncInfoForTheFailingNotebook();
  void test_viewAreaFailureIsKeylessSoEveryAttemptInterrupts();
  void test_imageHostFailureArrivesThroughTheRealConnection();
  void test_imageHostSuccessRetiresThatUploadsIncident();
  void test_retirementClearsAllThreeSyncKeysTogether();
  void test_bufferAutoSaveAbortIsInterrupting();
  void test_repeatedBufferAbortFoldsAndDoesNotReInterrupt();
  void test_bufferSlotSignaturesMatchBufferServiceSignals();
  void test_bufferSaveErrorIsPassive();
  void test_bufferSavedRetiresBufferIncidents();

private:
  const NotificationMessage *activeWithKey(const QString &p_key) const;
  int addedCount() const { return m_added; }

  ServiceLocator *m_services = nullptr;
  NotificationService *m_notifications = nullptr;
  ImageHostService *m_imageHost = nullptr;
  NotificationRouter *m_router = nullptr;
  int m_added = 0;
};

const NotificationMessage *TestNotificationRouter::activeWithKey(const QString &p_key) const {
  for (const auto &msg : m_notifications->messages()) {
    if (!msg.m_dismissed && msg.m_dedupKey == p_key) {
      return &msg;
    }
  }
  return nullptr;
}

void TestNotificationRouter::init() {
  m_services = new ServiceLocator();
  m_notifications = new NotificationService();
  m_services->registerService<NotificationService>(m_notifications);

  // ImageHostService is cheap to build (a null HookManager is tolerated), so it
  // is registered for real: that makes the image-host cases exercise the actual
  // connection the router's constructor makes, not just its policy.
  //
  // BufferService and SyncService are NOT registered -- both need a vxcore
  // context, and SyncService additionally needs the OS keychain (which
  // tests/AGENTS.md requires a KeychainGuard for). The router tolerates their
  // absence; their policy is driven through the public slots instead. See the
  // sync-retirement test for why that is sufficient for the pointer-to-member
  // connections, and the slot-signature test for the string-based ones.
  m_imageHost = new ImageHostService(nullptr);
  m_services->registerService<ImageHostService>(m_imageHost);

  m_router = new NotificationRouter(*m_services);

  m_added = 0;
  connect(m_notifications, &NotificationService::messageAdded, this,
          [this](const NotificationMessage &) { ++m_added; });
}

void TestNotificationRouter::cleanup() {
  delete m_router;
  m_router = nullptr;
  delete m_imageHost;
  m_imageHost = nullptr;
  delete m_notifications;
  m_notifications = nullptr;
  delete m_services;
  m_services = nullptr;
}

void TestNotificationRouter::test_syncAuthFailureIsInterruptingAndKeyed() {
  m_router->onSyncUserMessageRequested(QStringLiteral("nb1"), VXCORE_ERR_SYNC_AUTH_FAILED,
                                       QStringLiteral("Sync authentication failed"),
                                       QStringLiteral("body"), QStringLiteral("HTTP 401 detail"));

  const auto *msg = activeWithKey(QStringLiteral("sync.auth.nb1"));
  QVERIFY(msg);
  QCOMPARE(msg->m_category, QStringLiteral("sync"));
  QCOMPARE(msg->m_severity, NotificationMessage::Severity::Warning);
  QCOMPARE(msg->m_attention, NotificationMessage::Attention::Interrupt);
  QCOMPARE(msg->m_duration, NotificationMessage::Duration::Persist);
  // The long backend blob that used to be QMessageBox::setDetailedText.
  QCOMPARE(msg->m_details, QStringLiteral("HTTP 401 detail"));
  QCOMPARE(msg->m_actions.size(), 1);
}

// A network failure is transient and self-healing (VNote retries on the next
// change), so it must not steal attention.
void TestNotificationRouter::test_syncNetworkFailureIsPassive() {
  m_router->onSyncUserMessageRequested(QStringLiteral("nb1"), VXCORE_ERR_SYNC_NETWORK,
                                       QStringLiteral("Sync network error"),
                                       QStringLiteral("body"), QString());

  const auto *msg = activeWithKey(QStringLiteral("sync.network.nb1"));
  QVERIFY(msg);
  QCOMPARE(msg->m_severity, NotificationMessage::Severity::Info);
  QCOMPARE(msg->m_attention, NotificationMessage::Attention::Passive);
  QVERIFY(msg->m_actions.isEmpty());
}

void TestNotificationRouter::test_syncOtherFailureUsesTheGenericKey() {
  m_router->onSyncUserMessageRequested(QStringLiteral("nb1"), VXCORE_ERR_UNKNOWN,
                                       QStringLiteral("Sync failed"), QStringLiteral("body"),
                                       QString());

  QVERIFY(activeWithKey(QStringLiteral("sync.failed.nb1")));
  QVERIFY(!activeWithKey(QStringLiteral("sync.auth.nb1")));
}

// This is the anti-spam behaviour that used to be m_authFailureNotified: a
// repeat within the same incident folds into the live message, and since the
// toast is raised only by messageAdded, it stays quiet.
void TestNotificationRouter::test_repeatSyncFailureFoldsAndDoesNotInterrupt() {
  m_router->onSyncUserMessageRequested(QStringLiteral("nb1"), VXCORE_ERR_SYNC_AUTH_FAILED,
                                       QStringLiteral("t"), QStringLiteral("first"), QString());
  QCOMPARE(addedCount(), 1);

  m_router->onSyncUserMessageRequested(QStringLiteral("nb1"), VXCORE_ERR_SYNC_AUTH_FAILED,
                                       QStringLiteral("t"), QStringLiteral("second"), QString());
  QCOMPARE(addedCount(), 1);
  QCOMPARE(m_notifications->activeCount(), 1);
  QCOMPARE(activeWithKey(QStringLiteral("sync.auth.nb1"))->m_text, QStringLiteral("second"));
}

// The user explicitly clicked Sync Now, so the next failure MUST surface again.
// Without retirement it would fold into the live message and the user would see
// "nothing happens" -- the exact bug the old anti-spam clear existed to avoid.
void TestNotificationRouter::test_manualRetryRetiresTheIncidentSoTheNextFailureInterrupts() {
  m_router->onSyncUserMessageRequested(QStringLiteral("nb1"), VXCORE_ERR_SYNC_AUTH_FAILED,
                                       QStringLiteral("t"), QStringLiteral("first"), QString());
  QCOMPARE(addedCount(), 1);

  m_router->onSyncIncidentRetryRequested(QStringLiteral("nb1"));
  QVERIFY2(!activeWithKey(QStringLiteral("sync.auth.nb1")), "the incident was not retired");

  m_router->onSyncUserMessageRequested(QStringLiteral("nb1"), VXCORE_ERR_SYNC_AUTH_FAILED,
                                       QStringLiteral("t"), QStringLiteral("second"), QString());
  QCOMPARE(addedCount(), 2);
}

// Retirement is per-notebook. A boundary reached on one notebook must never
// delete another notebook's unresolved failure -- that record is still true.
void TestNotificationRouter::test_notebookSwitchDoesNotRetireAnotherNotebook() {
  m_router->onSyncUserMessageRequested(QStringLiteral("nb1"), VXCORE_ERR_SYNC_AUTH_FAILED,
                                       QStringLiteral("t"), QStringLiteral("a"), QString());
  m_router->onSyncUserMessageRequested(QStringLiteral("nb2"), VXCORE_ERR_SYNC_AUTH_FAILED,
                                       QStringLiteral("t"), QStringLiteral("b"), QString());
  QCOMPARE(m_notifications->activeCount(), 2);

  // The strongest per-notebook boundary there is.
  m_router->onSyncIncidentRetryRequested(QStringLiteral("nb1"));

  QVERIFY(!activeWithKey(QStringLiteral("sync.auth.nb1")));
  QVERIFY2(activeWithKey(QStringLiteral("sync.auth.nb2")),
           "retiring nb1 also retired nb2's unresolved failure");
}

// The action must open Sync Info for the notebook that FAILED, which may not be
// the one currently on screen.
void TestNotificationRouter::test_syncAuthActionEmitsOpenSyncInfoForTheFailingNotebook() {
  QSignalSpy spy(m_router, &NotificationRouter::openSyncInfoRequested);

  m_router->onSyncUserMessageRequested(QStringLiteral("nb-failing"),
                                       VXCORE_ERR_SYNC_AUTH_FAILED, QStringLiteral("t"),
                                       QStringLiteral("body"), QString());

  const auto *msg = activeWithKey(QStringLiteral("sync.auth.nb-failing"));
  QVERIFY(msg);
  QCOMPARE(msg->m_actions.size(), 1);
  msg->m_actions.at(0).m_callback();

  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("nb-failing"));
}

// Opening a file is user-initiated, so every attempt must give feedback. A
// dedup key would silence the second double-click, and there is no natural
// boundary at which to retire it.
void TestNotificationRouter::test_viewAreaFailureIsKeylessSoEveryAttemptInterrupts() {
  m_router->onViewWindowCreationFailed(QStringLiteral("weird"), QStringLiteral("a.weird"));
  m_router->onViewWindowCreationFailed(QStringLiteral("weird"), QStringLiteral("a.weird"));

  QCOMPARE(addedCount(), 2);
  QCOMPARE(m_notifications->activeCount(), 2);

  const auto &msg = m_notifications->messages().at(0);
  QVERIFY2(msg.m_dedupKey.isEmpty(), "the view-area message must stay keyless");
  QCOMPARE(msg.m_category, QStringLiteral("viewarea"));
  QCOMPARE(msg.m_attention, NotificationMessage::Attention::Interrupt);
}

// Auto-save has GIVEN UP: the user's work exists only in memory.
// Unlike the widget-owned sources, this one goes through the REAL connection
// the router's constructor makes -- so it fails if that connect() is dropped or
// mis-typed, not just if the policy is wrong.
void TestNotificationRouter::test_imageHostFailureArrivesThroughTheRealConnection() {
  ImageHostAsyncResult result;
  result.token = 7;
  result.success = false;
  result.fileName = QStringLiteral("shot.png");
  result.errorMessage = QStringLiteral("403 Forbidden");
  emit m_imageHost->uploadFinished(7, result);

  const auto *msg = activeWithKey(QStringLiteral("imagehost.upload.7"));
  QVERIFY2(msg, "the image-host failure never reached the router");
  QCOMPARE(msg->m_category, QStringLiteral("imagehost"));
  QCOMPARE(msg->m_severity, NotificationMessage::Severity::Error);
  QCOMPARE(msg->m_attention, NotificationMessage::Attention::Interrupt);
  QCOMPARE(msg->m_details, QStringLiteral("403 Forbidden"));

  // A different upload is a DISTINCT incident and must interrupt on its own.
  ImageHostAsyncResult other;
  other.token = 8;
  other.success = false;
  emit m_imageHost->uploadFinished(8, other);

  QCOMPARE(addedCount(), 2);
  QVERIFY(activeWithKey(QStringLiteral("imagehost.upload.8")));
}

void TestNotificationRouter::test_imageHostSuccessRetiresThatUploadsIncident() {
  ImageHostAsyncResult failure;
  failure.token = 3;
  failure.success = false;
  emit m_imageHost->uploadFinished(3, failure);
  QVERIFY(activeWithKey(QStringLiteral("imagehost.upload.3")));

  ImageHostAsyncResult success;
  success.token = 3;
  success.success = true;
  emit m_imageHost->uploadFinished(3, success);

  QVERIFY2(!activeWithKey(QStringLiteral("imagehost.upload.3")),
           "a successful upload did not retire its incident");
  // Success itself must not post anything.
  QCOMPARE(addedCount(), 1);
}

// The four SyncService-owned boundaries (sync / enable / credentials / disable
// success) all funnel into the same retirement policy, and each is connected
// with the compile-time-checked pointer-to-member form -- so what is worth
// pinning here is the POLICY: one boundary retires all three sync keys for that
// notebook, and only that notebook.
void TestNotificationRouter::test_retirementClearsAllThreeSyncKeysTogether() {
  m_router->onSyncUserMessageRequested(QStringLiteral("nb1"), VXCORE_ERR_SYNC_AUTH_FAILED,
                                       QStringLiteral("t"), QStringLiteral("a"), QString());
  m_router->onSyncUserMessageRequested(QStringLiteral("nb1"), VXCORE_ERR_SYNC_NETWORK,
                                       QStringLiteral("t"), QStringLiteral("b"), QString());
  m_router->onSyncUserMessageRequested(QStringLiteral("nb1"), VXCORE_ERR_UNKNOWN,
                                       QStringLiteral("t"), QStringLiteral("c"), QString());
  m_router->onSyncUserMessageRequested(QStringLiteral("nb2"), VXCORE_ERR_SYNC_AUTH_FAILED,
                                       QStringLiteral("t"), QStringLiteral("d"), QString());
  QCOMPARE(m_notifications->activeCount(), 4);

  m_router->onSyncIncidentRetryRequested(QStringLiteral("nb1"));

  QVERIFY(!activeWithKey(QStringLiteral("sync.auth.nb1")));
  QVERIFY(!activeWithKey(QStringLiteral("sync.network.nb1")));
  QVERIFY(!activeWithKey(QStringLiteral("sync.failed.nb1")));
  QVERIFY2(activeWithKey(QStringLiteral("sync.auth.nb2")),
           "retiring nb1's incidents also retired nb2's");
  QCOMPARE(m_notifications->activeCount(), 1);
}

void TestNotificationRouter::test_bufferAutoSaveAbortIsInterrupting() {
  m_router->onBufferAutoSaveAborted(QStringLiteral("buf1"));

  const auto *msg = activeWithKey(QStringLiteral("buffer.autosave.buf1"));
  QVERIFY(msg);
  QCOMPARE(msg->m_category, QStringLiteral("buffer"));
  QCOMPARE(msg->m_severity, NotificationMessage::Severity::Error);
  QCOMPARE(msg->m_attention, NotificationMessage::Attention::Interrupt);
  QCOMPARE(msg->m_duration, NotificationMessage::Duration::Persist);
}

// A single failure will be retried on the next tick; only the abort interrupts.
// BufferService re-emits bufferAutoSaveAborted on EVERY subsequent attempt once
// the failure count is past the threshold (bufferservice.cpp: the abort branch
// has no "only once" guard). If the router used renotify() here, each repeat
// would remove-and-repost and re-raise the toast on a loop -- the exact spam the
// incident model exists to prevent.
void TestNotificationRouter::test_repeatedBufferAbortFoldsAndDoesNotReInterrupt() {
  m_router->onBufferAutoSaveAborted(QStringLiteral("buf1"));
  QCOMPARE(addedCount(), 1);
  const quint64 firstId = activeWithKey(QStringLiteral("buffer.autosave.buf1"))->m_id;

  for (int i = 0; i < 5; ++i) {
    m_router->onBufferAutoSaveAborted(QStringLiteral("buf1"));
  }

  QCOMPARE(addedCount(), 1);
  QCOMPARE(m_notifications->activeCount(), 1);
  const auto *msg = activeWithKey(QStringLiteral("buffer.autosave.buf1"));
  QVERIFY(msg);
  QVERIFY2(msg->m_id == firstId, "the incident was replaced instead of folded");
}

// The buffer connections use the string-based SIGNAL/SLOT form (BufferService
// privately inherits QObject and exposes only a bare QObject*), so a typo is a
// RUNTIME failure that surfaces only when a user's auto-save is already
// failing. Pin the router's half of each signature here; the production ctor
// additionally qCritical()s if any connect() returns false.
void TestNotificationRouter::test_bufferSlotSignaturesMatchBufferServiceSignals() {
  const QMetaObject *mo = m_router->metaObject();
  for (const char *sig : {"onBufferAutoSaveAborted(QString)", "onBufferSaveError(QString,QString)",
                          "onBufferAutoSaved(QString)"}) {
    const QByteArray normalized = QMetaObject::normalizedSignature(sig);
    QVERIFY2(mo->indexOfSlot(normalized.constData()) >= 0,
             qPrintable(QStringLiteral("NotificationRouter has no slot %1")
                            .arg(QString::fromUtf8(normalized))));
  }
}

void TestNotificationRouter::test_bufferSaveErrorIsPassive() {
  m_router->onBufferSaveError(QStringLiteral("buf1"), QStringLiteral("disk full"));

  const auto *msg = activeWithKey(QStringLiteral("buffer.saveerror.buf1"));
  QVERIFY(msg);
  QCOMPARE(msg->m_attention, NotificationMessage::Attention::Passive);
  QCOMPARE(msg->m_details, QStringLiteral("disk full"));
}

void TestNotificationRouter::test_bufferSavedRetiresBufferIncidents() {
  m_router->onBufferAutoSaveAborted(QStringLiteral("buf1"));
  m_router->onBufferSaveError(QStringLiteral("buf1"), QStringLiteral("e"));
  QCOMPARE(m_notifications->activeCount(), 2);

  m_router->onBufferAutoSaved(QStringLiteral("buf1"));

  QVERIFY(!activeWithKey(QStringLiteral("buffer.autosave.buf1")));
  QVERIFY(!activeWithKey(QStringLiteral("buffer.saveerror.buf1")));

  // A later failure is a NEW incident and interrupts again.
  const int before = addedCount();
  m_router->onBufferAutoSaveAborted(QStringLiteral("buf1"));
  QCOMPARE(addedCount(), before + 1);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotificationRouter)
#include "test_notificationrouter.moc"
