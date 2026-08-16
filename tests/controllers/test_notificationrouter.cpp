// NotificationRouter: subsystem failure signals -> NotificationMessages.
//
// GUILESS. The router holds no widget pointers by design, so the widget-owned
// sources (NotebookExplorer2, ViewArea2) are driven through its public slots
// without constructing any widget.

#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>

#include <controllers/notificationrouter.h>
#include <core/configmgr2.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/imagehostservice.h>
#include <core/services/notificationservice.h>
#include <imagehost/imagehosttypes.h>

#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestNotificationRouter : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
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
  void test_extraDataFailuresAreRaisedOncePerFolderAtAfterStart();
  void test_noExtraDataFailureRaisesNothing();

private:
  const NotificationMessage *activeWithKey(const QString &p_key) const;
  int addedCount() const { return m_added; }

  // Build an on-disk stand-in for the bundled vnote_extra.rcc tree.
  QString buildExtraDataFixture(TempDirFixture &p_tmp) const;

  // Wipe the installed extra-data folders (and therefore their stamps).
  void resetInstalledExtraData() const;

  VxCoreContextHandle m_context = nullptr;
  ConfigCoreService *m_configService = nullptr;

  ServiceLocator *m_services = nullptr;
  NotificationService *m_notifications = nullptr;
  ImageHostService *m_imageHost = nullptr;
  HookManager *m_hookMgr = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
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

void TestNotificationRouter::initTestCase() {
  // CRITICAL: before any vxcore context is created, so the extra-data cases
  // install into an isolated temp app-data folder.
  vxcore_set_test_mode(1);

  QCOMPARE(vxcore_context_create(nullptr, &m_context), VXCORE_OK);
  QVERIFY(m_context != nullptr);
  m_configService = new ConfigCoreService(m_context);
}

void TestNotificationRouter::cleanupTestCase() {
  delete m_configService;
  m_configService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestNotificationRouter::init() {
  m_services = new ServiceLocator();
  m_notifications = new NotificationService();
  m_services->registerService<NotificationService>(m_notifications);

  // ImageHostService is cheap to build (a null HookManager is tolerated), so it
  // is registered for real: that makes the image-host cases exercise the actual
  // connection the router's constructor makes, not just its policy.
  //
  // BufferService and SyncService are NOT registered -- both need extra setup,
  // and SyncService additionally needs the OS keychain (which tests/AGENTS.md
  // requires a KeychainGuard for). The router tolerates their absence; their
  // policy is driven through the public slots instead. See the sync-retirement
  // test for why that is sufficient for the pointer-to-member connections, and
  // the slot-signature test for the string-based ones.
  m_imageHost = new ImageHostService(nullptr);
  m_services->registerService<ImageHostService>(m_imageHost);

  // The extra-data notification is a PULL at MainWindowAfterStart, so both the
  // hook bus and ConfigMgr2 must exist BEFORE the router subscribes.
  m_hookMgr = new HookManager();
  m_services->registerService<HookManager>(m_hookMgr);

  m_configMgr = new ConfigMgr2(m_configService);
  m_services->registerService<ConfigMgr2>(m_configMgr);

  m_router = new NotificationRouter(*m_services);

  m_added = 0;
  connect(m_notifications, &NotificationService::messageAdded, this,
          [this](const NotificationMessage &) { ++m_added; });
}

void TestNotificationRouter::cleanup() {
  // Router first: its destructor unsubscribes from the HookManager.
  delete m_router;
  m_router = nullptr;
  delete m_configMgr;
  m_configMgr = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
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
                                       QStringLiteral("Sync network error"), QStringLiteral("body"),
                                       QString());

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

  m_router->onSyncUserMessageRequested(QStringLiteral("nb-failing"), VXCORE_ERR_SYNC_AUTH_FAILED,
                                       QStringLiteral("t"), QStringLiteral("body"), QString());

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

// =============================================================================
// Extra-data install failures (pulled from ConfigMgr2 at MainWindowAfterStart)
// =============================================================================

QString TestNotificationRouter::buildExtraDataFixture(TempDirFixture &p_tmp) const {
  const QString root = p_tmp.createDir("extra");
  for (const char *folder :
       {"themes", "tasks", "syntax-highlighting", "web", "dicts", "templates"}) {
    const QString name = QString::fromLatin1(folder);
    p_tmp.createDir(QStringLiteral("extra/") + name);
    p_tmp.createTextFile(QStringLiteral("extra/%1/marker.txt").arg(name),
                         QStringLiteral("bundled %1").arg(name));
  }
  return root;
}

void TestNotificationRouter::resetInstalledExtraData() const {
  for (auto type : {ConfigMgr2::ConfigDataType::Themes, ConfigMgr2::ConfigDataType::Tasks,
                    ConfigMgr2::ConfigDataType::SyntaxHighlighting, ConfigMgr2::ConfigDataType::Web,
                    ConfigMgr2::ConfigDataType::Dicts, ConfigMgr2::ConfigDataType::Templates}) {
    QDir(m_configMgr->getConfigDataFolder(type)).removeRecursively();
  }
}

// ConfigMgr2 dumps the bundled data in main(), long before this router exists,
// so the failures are PULLED once at MainWindowAfterStart rather than pushed.
void TestNotificationRouter::test_extraDataFailuresAreRaisedOncePerFolderAtAfterStart() {
  resetInstalledExtraData();

  TempDirFixture tmp;
  QVERIFY(tmp.isValid());
  const QString fixture = buildExtraDataFixture(tmp);

  // Failure injection: a DIRECTORY where <folder>/marker.txt must land.
  QStringList blockers;
  for (auto type : {ConfigMgr2::ConfigDataType::Web, ConfigMgr2::ConfigDataType::Dicts}) {
    const QString blocker = m_configMgr->getConfigDataFolder(type) + QStringLiteral("/marker.txt");
    QVERIFY(QDir().mkpath(blocker));
    blockers.append(blocker);
  }

  m_configMgr->setExtraDataSourceRootOverrideForTesting(fixture);
  m_configMgr->init();
  m_configMgr->initAfterQtAppStarted();
  QCOMPARE(m_configMgr->extraDataCopyFailures().size(), 2);

  // Nothing is raised until the hook fires.
  QCOMPARE(addedCount(), 0);

  QVariantMap args;
  m_hookMgr->doAction(HookNames::MainWindowAfterStart, args);

  QCOMPARE(addedCount(), 2);
  QCOMPARE(m_notifications->activeCount(), 2);

  QStringList named;
  for (const auto &msg : m_notifications->messages()) {
    QCOMPARE(msg.m_category, QStringLiteral("config"));
    QVERIFY2(msg.m_dedupKey.isEmpty(), "the extra-data message must stay keyless");
    QCOMPARE(msg.m_severity, NotificationMessage::Severity::Warning);
    QCOMPARE(msg.m_attention, NotificationMessage::Attention::Interrupt);
    QCOMPARE(msg.m_duration, NotificationMessage::Duration::Long);
    // Nothing safe for the user to click.
    QVERIFY(msg.m_actions.isEmpty());
    // The failed paths belong in the collapsible detail section, for EVERY
    // failed folder -- not just the first one.
    QVERIFY2(msg.m_details.contains(QStringLiteral("marker.txt")), qPrintable(msg.m_details));
    if (msg.m_text.contains(QStringLiteral("web"))) {
      named.append(QStringLiteral("web"));
    } else if (msg.m_text.contains(QStringLiteral("dicts"))) {
      named.append(QStringLiteral("dicts"));
    }
  }
  named.sort();
  QCOMPARE(named, QStringList({QStringLiteral("dicts"), QStringLiteral("web")}));

  for (const auto &blocker : blockers) {
    QVERIFY(QDir(blocker).removeRecursively());
  }
}

void TestNotificationRouter::test_noExtraDataFailureRaisesNothing() {
  resetInstalledExtraData();

  TempDirFixture tmp;
  QVERIFY(tmp.isValid());
  const QString fixture = buildExtraDataFixture(tmp);

  m_configMgr->setExtraDataSourceRootOverrideForTesting(fixture);
  m_configMgr->init();
  m_configMgr->initAfterQtAppStarted();
  QVERIFY(m_configMgr->extraDataCopyFailures().isEmpty());

  QVariantMap args;
  m_hookMgr->doAction(HookNames::MainWindowAfterStart, args);

  QCOMPARE(addedCount(), 0);
  QCOMPARE(m_notifications->activeCount(), 0);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotificationRouter)
#include "test_notificationrouter.moc"
