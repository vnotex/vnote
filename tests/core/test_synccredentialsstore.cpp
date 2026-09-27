// test_synccredentialsstore.cpp - Unit tests for SyncCredentialsStore (T4)
//
// Per ADR-9, SyncCredentialsStore stores PATs exclusively in QtKeychain with
// NO plaintext fallback. This test exercises:
//   - store/retrieve roundtrip (when keychain available)
//   - delete roundtrip (when keychain available)
//   - keychain-unavailable error path (skipped when keychain available)
//   - PAT-not-logged property (verified by capturing all qDebug/qWarning/etc
//     output via qInstallMessageHandler)

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QtTest>

#include <test_helper.h>

#include <core/servicelocator.h>
#include <core/services/synccredentialsstore.h>
#include <vxcore/vxcore.h>

#include "../helpers/keychain_guard.h"

#ifdef VNOTE_KEYCHAIN_AVAILABLE
#include <keychain.h>
#endif

using namespace vnotex;

namespace tests {

// Captures messages from qInstallMessageHandler for the patNotLogged test.
namespace {
QString g_logCapture;
QtMessageHandler g_previousHandler = nullptr;

void captureMessageHandler(QtMsgType p_type, const QMessageLogContext &p_ctx,
                           const QString &p_msg) {
  Q_UNUSED(p_type);
  Q_UNUSED(p_ctx);
  g_logCapture += p_msg;
  g_logCapture += QLatin1Char('\n');
}

#ifdef VNOTE_KEYCHAIN_AVAILABLE
bool writeRawVault(const QString &p_id, const QString &p_value) {
  QObject receiver;
  QKeychain::Error error = QKeychain::OtherError;
  auto *job = new QKeychain::WritePasswordJob(SyncCredentialsStore::serviceName());
  job->setInsecureFallback(false);
  job->setKey(SyncCredentialsStore::keychainKey(p_id));
  job->setTextData(p_value);
  QObject::connect(job, &QKeychain::Job::finished, &receiver,
                   [&error](QKeychain::Job *p_job) { error = p_job->error(); });
  QSignalSpy finished(job, &QKeychain::Job::finished);
  job->start();
  return (!finished.isEmpty() || finished.wait(5000)) && error == QKeychain::NoError;
}

bool readRawVault(const QString &p_id, QString &p_value) {
  QObject receiver;
  QKeychain::Error error = QKeychain::OtherError;
  auto *job = new QKeychain::ReadPasswordJob(SyncCredentialsStore::serviceName());
  job->setInsecureFallback(false);
  job->setKey(SyncCredentialsStore::keychainKey(p_id));
  QObject::connect(job, &QKeychain::Job::finished, &receiver,
                   [&error, &p_value, job](QKeychain::Job *) {
                     error = job->error();
                     if (error == QKeychain::NoError)
                       p_value = job->textData();
                   });
  QSignalSpy finished(job, &QKeychain::Job::finished);
  job->start();
  return (!finished.isEmpty() || finished.wait(5000)) && error == QKeychain::NoError;
}
#endif
} // namespace

class TestSyncCredentialsStore : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void keychainKeyFormat();
  void serviceNameValue();
  void storeRetrieve();
  void delete_();
  void deleteMissingEntryEmitsDeleted();
  void keychainUnavailableEmitsError();
  void patNotLogged();
  void legacyGitEntryRemainsRaw();
  void webdavEnvelopeSurvivesFreshStoreAndQueuedDelivery();
  void malformedEnvelopeIsRedacted_data();
  void malformedEnvelopeIsRedacted();
  void invalidTypedCredentialUsesStoreError();

  // W2.T0 (sync-completion-flow-overhaul) — synchronous hasCredentials cache.
  void testHasCredentialsAfterStore();
  void testHasCredentialsAfterDelete();
  void testHasCredentialsForUnknownIdReturnsFalse();
  void testRefreshKnownIdsPopulatesCache();

  // Regression gate for the cross-thread QObject parenting warning that
  // fires when the three async credential methods are invoked off the
  // store's thread (e.g., from a NotebookAfterClose/AfterOpen hook handler
  // fired inside OpenNotebookController::cloneAndOpen's QtConcurrent::run
  // worker). See plan: .sisyphus/plans/synccredentialsstore-thread-affinity.md
  void testStoreSafeFromWorkerThread();
  void testRetrieveSafeFromWorkerThread();
  void testDeleteSafeFromWorkerThread();

private:
  // Helper: wait for either signal A or signal B; return which fired first.
  // Returns 1 if signalA fires, 2 if signalB fires, 0 on timeout.
  int waitForEither(QSignalSpy &p_a, QSignalSpy &p_b, int p_timeoutMs);

  VxCoreContextHandle m_context = nullptr;
  ServiceLocator m_services;
};

void TestSyncCredentialsStore::initTestCase() {
  // CRITICAL: enable test mode BEFORE creating vxcore context (per tests/AGENTS.md)
  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QVERIFY2(err == VXCORE_OK, "Failed to create vxcore context");
  QVERIFY(m_context != nullptr);
}

void TestSyncCredentialsStore::cleanupTestCase() {
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestSyncCredentialsStore::keychainKeyFormat() {
  QCOMPARE(SyncCredentialsStore::keychainKey(QStringLiteral("nb_42")),
           QStringLiteral("notebook_sync_pat_nb_42"));
  QCOMPARE(SyncCredentialsStore::keychainKey(QStringLiteral("")),
           QStringLiteral("notebook_sync_pat_"));
}

void TestSyncCredentialsStore::serviceNameValue() {
  QCOMPARE(SyncCredentialsStore::serviceName(), QStringLiteral("VNote"));
}

int TestSyncCredentialsStore::waitForEither(QSignalSpy &p_a, QSignalSpy &p_b, int p_timeoutMs) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < p_timeoutMs) {
    if (!p_a.isEmpty()) {
      return 1;
    }
    if (!p_b.isEmpty()) {
      return 2;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QTest::qWait(10);
  }
  if (!p_a.isEmpty()) {
    return 1;
  }
  if (!p_b.isEmpty()) {
    return 2;
  }
  return 0;
}

void TestSyncCredentialsStore::legacyGitEntryRemainsRaw() {
#ifndef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("Requires QtKeychain and an unlocked OS vault; not credential verification");
#else
  SyncCredentialsStore store(m_services);
  KeychainGuard guard(&store);
  const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto pat = QStringLiteral(" legacy Git token with whitespace ");
  guard.track(id);
  QVERIFY2(writeRawVault(id, pat), "Requires a usable unlocked OS vault");
  QSignalSpy retrieved(&store, &SyncCredentialsStore::credentialsRetrieved);
  QSignalSpy errors(&store, &SyncCredentialsStore::credentialsError);
  store.retrieveCredentials(id);
  QCOMPARE(waitForEither(retrieved, errors, 5000), 1);
  const auto credentials = qvariant_cast<SyncCredential>(retrieved.first().at(1));
  QCOMPARE(credentials.m_backend, QStringLiteral("git"));
  QCOMPARE(credentials.m_secret, pat);
  QVERIFY(credentials.m_username.isEmpty());
  QSignalSpy stored(&store, &SyncCredentialsStore::credentialsStored);
  QSignalSpy storeErrors(&store, &SyncCredentialsStore::credentialsStoreError);
  store.storeCredentials(id, credentials);
  QCOMPARE(waitForEither(stored, storeErrors, 5000), 1);
  QString raw;
  QVERIFY(readRawVault(id, raw));
  QCOMPARE(raw, pat);
  guard.cleanup();
#endif
}

void TestSyncCredentialsStore::webdavEnvelopeSurvivesFreshStoreAndQueuedDelivery() {
#ifndef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("Requires QtKeychain and an unlocked OS vault; not credential verification");
#else
  const auto inheritedId = QString::fromUtf8(qgetenv("VNOTE_CREDENTIAL_RESTART_TEST_ID"));
  const auto id =
      inheritedId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : inheritedId;
  const SyncCredential credentials{QStringLiteral("webdav"), QString::fromUtf8("用户 name"),
                                   QStringLiteral(" app-password\\\"\n ")};
  SyncCredentialsStore fresh(m_services);
  KeychainGuard guard(&fresh);
  if (inheritedId.isEmpty())
    guard.track(id);
  g_logCapture.clear();
  const auto previous = qInstallMessageHandler(captureMessageHandler);
  const auto restore = qScopeGuard([previous]() { qInstallMessageHandler(previous); });
  if (inheritedId.isEmpty()) {
    SyncCredentialsStore first(m_services);
    QSignalSpy stored(&first, &SyncCredentialsStore::credentialsStored);
    QSignalSpy errors(&first, &SyncCredentialsStore::credentialsStoreError);
    first.storeCredentials(id, credentials);
    QCOMPARE(waitForEither(stored, errors, 5000), 1);
  }
  QString raw;
  QVERIFY(readRawVault(id, raw));
  const auto prefix = QStringLiteral("vnote-sync-credentials-v1\n");
  QVERIFY(raw.startsWith(prefix));
  const auto envelope = QJsonDocument::fromJson(raw.mid(prefix.size()).toUtf8()).object();
  QCOMPARE(envelope.value(QStringLiteral("backend")).toString(), credentials.m_backend);
  QCOMPARE(envelope.value(QStringLiteral("username")).toString(), credentials.m_username);
  QCOMPARE(envelope.value(QStringLiteral("secret")).toString(), credentials.m_secret);
  if (inheritedId.isEmpty()) {
    QTemporaryDir childTemp;
    QVERIFY(childTemp.isValid());
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("VNOTE_CREDENTIAL_RESTART_TEST_ID"), id);
    for (const auto &name :
         {QStringLiteral("TMP"), QStringLiteral("TEMP"), QStringLiteral("TMPDIR")})
      environment.insert(name, childTemp.path());
    QProcess child;
    child.setProcessEnvironment(environment);
    child.start(QCoreApplication::applicationFilePath(),
                {QStringLiteral("webdavEnvelopeSurvivesFreshStoreAndQueuedDelivery")});
    QVERIFY(child.waitForStarted(5000));
    QVERIFY(child.waitForFinished(20000));
    const auto output = child.readAllStandardOutput() + child.readAllStandardError();
    QVERIFY(!output.contains(credentials.m_secret.toUtf8()));
    QCOMPARE(child.exitStatus(), QProcess::NormalExit);
    QCOMPARE(child.exitCode(), 0);
  }
  bool delivered = false;
  SyncCredential received;
  connect(
      &fresh, &SyncCredentialsStore::credentialsRetrieved, &fresh,
      [&](const QString &p_id, const SyncCredential &p_credential) {
        if (p_id == id) {
          received = p_credential;
          delivered = true;
        }
      },
      Qt::QueuedConnection);
  fresh.retrieveCredentials(id);
  QTRY_VERIFY(delivered);
  QCOMPARE(received.m_backend, credentials.m_backend);
  QCOMPARE(received.m_username, credentials.m_username);
  QCOMPARE(received.m_secret, credentials.m_secret);
  QVERIFY(fresh.hasCredentials(id));
  QVERIFY(QMetaType::type("SyncCredential") != QMetaType::UnknownType);
  QVERIFY(QMetaType::type("vnotex::SyncCredential") != QMetaType::UnknownType);
  QVERIFY(!g_logCapture.contains(credentials.m_secret));
  QVERIFY(!g_logCapture.contains(credentials.m_username));
  guard.cleanup();
#endif
}

void TestSyncCredentialsStore::malformedEnvelopeIsRedacted_data() {
  QTest::addColumn<QString>("payload");
  QTest::newRow("unknown-version") << QStringLiteral("vnote-sync-credentials-v2\nsecret-sentinel");
  QTest::newRow("invalid-json") << QStringLiteral(
      "vnote-sync-credentials-v1\n{\"secret\":\"secret-sentinel");
  QTest::newRow("unknown-backend") << QStringLiteral(
      "vnote-sync-credentials-v1\n"
      "{\"backend\":\"other\",\"username\":\"user\",\"secret\":\"secret-sentinel\"}");
  QTest::newRow("git-is-not-an-envelope") << QStringLiteral(
      "vnote-sync-credentials-v1\n"
      "{\"backend\":\"git\",\"username\":\"user\",\"secret\":\"secret-sentinel\"}");
}

void TestSyncCredentialsStore::malformedEnvelopeIsRedacted() {
#ifndef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("Requires QtKeychain and an unlocked OS vault; not credential verification");
#else
  QFETCH(QString, payload);
  SyncCredentialsStore store(m_services);
  KeychainGuard guard(&store);
  const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  guard.track(id);
  QVERIFY2(writeRawVault(id, payload), "Requires a usable unlocked OS vault");
  QSignalSpy retrieved(&store, &SyncCredentialsStore::credentialsRetrieved);
  QSignalSpy errors(&store, &SyncCredentialsStore::credentialsError);
  g_logCapture.clear();
  const auto previous = qInstallMessageHandler(captureMessageHandler);
  const auto restore = qScopeGuard([previous]() { qInstallMessageHandler(previous); });
  store.retrieveCredentials(id);
  QCOMPARE(waitForEither(retrieved, errors, 5000), 2);
  QCOMPARE(retrieved.count(), 0);
  QVERIFY(!store.hasCredentials(id));
  const auto error = errors.first().at(1).toString();
  QVERIFY(!error.contains(QStringLiteral("secret-sentinel")));
  QVERIFY(!error.contains(payload));
  QVERIFY(!g_logCapture.contains(QStringLiteral("secret-sentinel")));
  guard.cleanup();
#endif
}

void TestSyncCredentialsStore::invalidTypedCredentialUsesStoreError() {
  SyncCredentialsStore store(m_services);
  QSignalSpy stored(&store, &SyncCredentialsStore::credentialsStored);
  QSignalSpy storeErrors(&store, &SyncCredentialsStore::credentialsStoreError);
  QSignalSpy unrelatedErrors(&store, &SyncCredentialsStore::credentialsError);
  store.storeCredentials(QStringLiteral("invalid-backend"),
                         {QStringLiteral("unknown"), QString(), QStringLiteral("secret")});
  QTRY_COMPARE(storeErrors.count(), 1);
  QCOMPARE(stored.count(), 0);
  QCOMPARE(unrelatedErrors.count(), 0);
  QVERIFY(!store.hasCredentials(QStringLiteral("invalid-backend")));
}

void TestSyncCredentialsStore::storeRetrieve() {
#ifndef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("VNOTE_KEYCHAIN_AVAILABLE not set: cannot test happy path");
#else
  SyncCredentialsStore store(m_services);
  tests::KeychainGuard guard(&store);
  const QString notebookId = QStringLiteral("nb_t4_storeretrieve");
  const QString pat = QStringLiteral("ghp_TEST123");

  // Best-effort cleanup of any leftover key from a prior aborted run.
  {
    QSignalSpy delDone(&store, &SyncCredentialsStore::credentialsDeleted);
    QSignalSpy delErr(&store, &SyncCredentialsStore::credentialsError);
    store.deleteCredentials(notebookId);
    waitForEither(delDone, delErr, 5000);
  }

  // Store
  QSignalSpy storedSpy(&store, &SyncCredentialsStore::credentialsStored);
  QSignalSpy errorSpy(&store, &SyncCredentialsStore::credentialsStoreError);
  store.storeCredentials(notebookId, {QStringLiteral("git"), QString(), pat});
  int which = waitForEither(storedSpy, errorSpy, 5000);
  if (which == 2) {
    const QString errMsg = errorSpy.first().at(1).toString();
    QSKIP(qPrintable(
        QStringLiteral("OS keychain backend not usable in this test environment: %1").arg(errMsg)));
  }
  QCOMPARE(which, 1);
  QCOMPARE(storedSpy.first().at(0).toString(), notebookId);

  // Retrieve
  QSignalSpy retrievedSpy(&store, &SyncCredentialsStore::credentialsRetrieved);
  QSignalSpy errorSpy2(&store, &SyncCredentialsStore::credentialsError);
  store.retrieveCredentials(notebookId);
  which = waitForEither(retrievedSpy, errorSpy2, 5000);
  QCOMPARE(which, 1);
  QCOMPARE(retrievedSpy.first().at(0).toString(), notebookId);
  QCOMPARE(qvariant_cast<SyncCredential>(retrievedSpy.first().at(1)).m_secret, pat);

  // Start a native read, then destroy its facade before Apple's main-queue
  // callback runs. The in-flight job must survive and let the next job finish.
  {
    SyncCredentialsStore transientStore(m_services);
    transientStore.retrieveCredentials(notebookId);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
  }
  retrievedSpy.clear();
  store.retrieveCredentials(notebookId);
  QCOMPARE(waitForEither(retrievedSpy, errorSpy2, 5000), 1);
  QCOMPARE(qvariant_cast<SyncCredential>(retrievedSpy.first().at(1)).m_secret, pat);

  // POST-test cleanup: delete what THIS test wrote
  guard.cleanup();
#endif
}

void TestSyncCredentialsStore::delete_() {
#ifndef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("VNOTE_KEYCHAIN_AVAILABLE not set: cannot test happy path");
#else
  SyncCredentialsStore store(m_services);
  tests::KeychainGuard guard(&store);
  const QString notebookId = QStringLiteral("nb_t4_deletetest");
  const QString pat = QStringLiteral("ghp_DELETEME");

  // Store first
  QSignalSpy storedSpy(&store, &SyncCredentialsStore::credentialsStored);
  QSignalSpy errSpy1(&store, &SyncCredentialsStore::credentialsStoreError);
  store.storeCredentials(notebookId, {QStringLiteral("git"), QString(), pat});
  int which = waitForEither(storedSpy, errSpy1, 5000);
  if (which == 2) {
    const QString errMsg = errSpy1.first().at(1).toString();
    QSKIP(qPrintable(
        QStringLiteral("OS keychain backend not usable in this test environment: %1").arg(errMsg)));
  }
  QCOMPARE(which, 1);

  // Delete
  QSignalSpy deletedSpy(&store, &SyncCredentialsStore::credentialsDeleted);
  QSignalSpy errSpy2(&store, &SyncCredentialsStore::credentialsError);
  store.deleteCredentials(notebookId);
  which = waitForEither(deletedSpy, errSpy2, 5000);
  QCOMPARE(which, 1);
  QCOMPARE(deletedSpy.first().at(0).toString(), notebookId);

  // A missing entry cannot produce usable credentials.
  QSignalSpy retrievedSpy(&store, &SyncCredentialsStore::credentialsRetrieved);
  QSignalSpy errSpy3(&store, &SyncCredentialsStore::credentialsError);
  store.retrieveCredentials(notebookId);
  which = waitForEither(retrievedSpy, errSpy3, 5000);
  QCOMPARE(which, 2);
  QCOMPARE(retrievedSpy.count(), 0);

  // POST-test cleanup: guard tracks the store() call above
  guard.cleanup();
#endif
}

void TestSyncCredentialsStore::deleteMissingEntryEmitsDeleted() {
#ifndef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("VNOTE_KEYCHAIN_AVAILABLE not set: cannot exercise real keychain delete");
#else
  // Regression for issue #2718: deleting a PAT that is not present must be
  // idempotent (credentialsDeleted, NOT credentialsError). On macOS the Apple
  // DeletePasswordJob reports errSecItemNotFound as an error; the store now
  // normalizes QKeychain::EntryNotFound to success on all platforms.
  SyncCredentialsStore store(m_services);
  tests::KeychainGuard guard(&store);
  const QString notebookId =
      QStringLiteral("nb_never_stored_") + QString::number(QDateTime::currentMSecsSinceEpoch());

  // Probe backend usability first with a real store, then delete it, so the
  // notebookId is guaranteed absent for the idempotency assertion below. A
  // headless CI runner with no Secret Service daemon ("org.freedesktop.secrets
  // was not provided") fails this store and skips — WITHOUT this probe a blanket
  // "credentialsError => skip" would also swallow the very macOS regression this
  // test guards (issue #2718: Apple's DeletePasswordJob reports errSecItemNotFound
  // as an error that the store MUST normalize to credentialsDeleted). On macOS,
  // Windows, and a keyring-backed Linux the backend is usable, so we never skip
  // and the normalization contract is enforced.
  {
    QSignalSpy storedSpy(&store, &SyncCredentialsStore::credentialsStored);
    QSignalSpy storeErrSpy(&store, &SyncCredentialsStore::credentialsStoreError);
    store.storeCredentials(notebookId,
                           {QStringLiteral("git"), QString(), QStringLiteral("ghp_PROBE")});
    const int stored = waitForEither(storedSpy, storeErrSpy, 5000);
    if (stored == 2) {
      const QString errMsg = storeErrSpy.first().at(1).toString();
      QSKIP(qPrintable(QStringLiteral("OS keychain backend not usable in this test environment: %1")
                           .arg(errMsg)));
    }
    QCOMPARE(stored, 1);

    QSignalSpy seedDelSpy(&store, &SyncCredentialsStore::credentialsDeleted);
    QSignalSpy seedDelErrSpy(&store, &SyncCredentialsStore::credentialsError);
    store.deleteCredentials(notebookId);
    QCOMPARE(waitForEither(seedDelSpy, seedDelErrSpy, 5000), 1);
  }

  // Backend is usable and the entry is now absent: deleting it again must be
  // idempotent (credentialsDeleted, NOT credentialsError).
  QSignalSpy deletedSpy(&store, &SyncCredentialsStore::credentialsDeleted);
  QSignalSpy errorSpy(&store, &SyncCredentialsStore::credentialsError);
  store.deleteCredentials(notebookId);
  const int which = waitForEither(deletedSpy, errorSpy, 5000);
  QCOMPARE(which, 1);
  QCOMPARE(deletedSpy.count(), 1);
  QCOMPARE(deletedSpy.first().at(0).toString(), notebookId);
  QCOMPARE(errorSpy.count(), 0);

  guard.cleanup();
#endif
}

void TestSyncCredentialsStore::keychainUnavailableEmitsError() {
#ifdef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("requires VNOTE_USE_KEYCHAIN=OFF build to test fallback path");
#else
  SyncCredentialsStore store(m_services);
  tests::KeychainGuard guard(&store);
  const QString notebookId = QStringLiteral("nb_t4_unavailable");

  QSignalSpy errorSpy(&store, &SyncCredentialsStore::credentialsStoreError);
  store.storeCredentials(notebookId, {QStringLiteral("git"), QString(), QStringLiteral("any_pat")});
  QVERIFY(errorSpy.wait(5000));
  QCOMPARE(errorSpy.count(), 1);
  QCOMPARE(errorSpy.first().at(0).toString(), notebookId);
  QVERIFY(
      errorSpy.first().at(1).toString().contains(QStringLiteral("secure-keychain-unavailable")));

  // POST-test cleanup
  guard.cleanup();
#endif
}

void TestSyncCredentialsStore::patNotLogged() {
  // Install handler capturing all log output.
  g_logCapture.clear();
  g_previousHandler = qInstallMessageHandler(captureMessageHandler);

  SyncCredentialsStore store(m_services);
  tests::KeychainGuard guard(&store);
  const QString notebookId = QStringLiteral("nb_t4_logleakcheck");
  const QString uniquePat = QStringLiteral("uniqueLeakCheck1234567890");

  // Roundtrip: store + retrieve + delete. Whatever the outcome (success or
  // error path), the PAT MUST NOT appear in any captured log message.
  {
    QSignalSpy doneSpy(&store, &SyncCredentialsStore::credentialsStored);
    QSignalSpy errSpy(&store, &SyncCredentialsStore::credentialsStoreError);
    store.storeCredentials(notebookId, {QStringLiteral("git"), QString(), uniquePat});
    waitForEither(doneSpy, errSpy, 5000);
  }
  {
    QSignalSpy doneSpy(&store, &SyncCredentialsStore::credentialsRetrieved);
    QSignalSpy errSpy(&store, &SyncCredentialsStore::credentialsError);
    store.retrieveCredentials(notebookId);
    waitForEither(doneSpy, errSpy, 5000);
  }
  {
    QSignalSpy doneSpy(&store, &SyncCredentialsStore::credentialsDeleted);
    QSignalSpy errSpy(&store, &SyncCredentialsStore::credentialsError);
    store.deleteCredentials(notebookId);
    waitForEither(doneSpy, errSpy, 5000);
  }

  // Restore previous handler before the assertion so a failure message itself
  // does not bypass the original handler chain.
  qInstallMessageHandler(g_previousHandler);
  g_previousHandler = nullptr;

  QVERIFY2(!g_logCapture.contains(QStringLiteral("uniqueLeakCheck1234567890")),
           "PAT value leaked into a log message");

  // POST-test cleanup
  guard.cleanup();
}

// ============================================================================
// W2.T0 — Synchronous hasCredentials() cache tests
// ============================================================================
// These tests cover the in-memory existence cache introduced for the UI
// classifier (W4.T1: paint-time detection of "disk says sync enabled but no
// PAT in keychain"). Cache invariants under exercise:
//   - storeCredentials success  -> cache contains id
//   - deleteCredentials success -> cache does not contain id
//   - unknown id (never stored) -> cache miss returns false
//   - retrieveCredentials probe -> cache (re)populated even on a fresh
//                                  SyncCredentialsStore instance
//
// PAT value is the literal "test_pat_12345"; W5.T3 secrecy-audit grep keys on
// this exact string.

void TestSyncCredentialsStore::testHasCredentialsAfterStore() {
#ifndef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("VNOTE_KEYCHAIN_AVAILABLE not set: cannot exercise cache via real store");
#else
  SyncCredentialsStore store(m_services);
  tests::KeychainGuard guard(&store);
  const QString notebookId = QStringLiteral("nb_w2t0_after_store");
  const QString pat = QStringLiteral("test_pat_12345");

  // Best-effort cleanup of any leftover key from a prior aborted run.
  {
    QSignalSpy delDone(&store, &SyncCredentialsStore::credentialsDeleted);
    QSignalSpy delErr(&store, &SyncCredentialsStore::credentialsError);
    store.deleteCredentials(notebookId);
    waitForEither(delDone, delErr, 5000);
  }

  // Sanity: cache empty before store.
  QVERIFY(!store.hasCredentials(notebookId));

  QSignalSpy storedSpy(&store, &SyncCredentialsStore::credentialsStored);
  QSignalSpy errorSpy(&store, &SyncCredentialsStore::credentialsStoreError);
  store.storeCredentials(notebookId, {QStringLiteral("git"), QString(), pat});
  int which = waitForEither(storedSpy, errorSpy, 5000);
  if (which == 2) {
    const QString errMsg = errorSpy.first().at(1).toString();
    QSKIP(qPrintable(
        QStringLiteral("OS keychain backend not usable in this test environment: %1").arg(errMsg)));
  }
  QCOMPARE(which, 1);

  // Cache must have been updated by the internal connect on credentialsStored
  // BEFORE the spy observed the signal (connections run in registration order;
  // cache lambda was registered in the store's constructor).
  QVERIFY2(store.hasCredentials(notebookId),
           "hasCredentials must return true immediately after credentialsStored");

  // POST-test cleanup: delete what THIS test wrote
  guard.cleanup();
#endif
}

void TestSyncCredentialsStore::testHasCredentialsAfterDelete() {
#ifndef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("VNOTE_KEYCHAIN_AVAILABLE not set: cannot exercise cache via real store");
#else
  SyncCredentialsStore store(m_services);
  tests::KeychainGuard guard(&store);
  const QString notebookId = QStringLiteral("nb_w2t0_after_delete");
  const QString pat = QStringLiteral("test_pat_12345");

  // Seed.
  {
    QSignalSpy storedSpy(&store, &SyncCredentialsStore::credentialsStored);
    QSignalSpy errSpy(&store, &SyncCredentialsStore::credentialsStoreError);
    store.storeCredentials(notebookId, {QStringLiteral("git"), QString(), pat});
    int which = waitForEither(storedSpy, errSpy, 5000);
    if (which == 2) {
      const QString errMsg = errSpy.first().at(1).toString();
      QSKIP(qPrintable(QStringLiteral("OS keychain backend not usable in this test environment: %1")
                           .arg(errMsg)));
    }
    QCOMPARE(which, 1);
  }
  QVERIFY(store.hasCredentials(notebookId));

  // Delete and verify cache eviction.
  QSignalSpy deletedSpy(&store, &SyncCredentialsStore::credentialsDeleted);
  QSignalSpy errSpy(&store, &SyncCredentialsStore::credentialsError);
  store.deleteCredentials(notebookId);
  int which = waitForEither(deletedSpy, errSpy, 5000);
  QCOMPARE(which, 1);

  QVERIFY2(!store.hasCredentials(notebookId),
           "hasCredentials must return false immediately after credentialsDeleted");

  // POST-test cleanup: guard tracks the seed store() call
  guard.cleanup();
#endif
}

void TestSyncCredentialsStore::testHasCredentialsForUnknownIdReturnsFalse() {
  // Pure cache-miss assertion; runs without keychain because no async I/O is
  // required.
  SyncCredentialsStore store(m_services);
  const QString unknownId = QStringLiteral("nb_w2t0_never_stored_12345abcdef");

  QVERIFY2(!store.hasCredentials(unknownId),
           "hasCredentials must return false for an id that was never stored");
  QVERIFY2(!store.hasCredentials(QString()), "hasCredentials must return false for an empty id");
}

void TestSyncCredentialsStore::testRefreshKnownIdsPopulatesCache() {
#ifndef VNOTE_KEYCHAIN_AVAILABLE
  // Without keychain the cache cannot self-populate. We can still verify that
  // refreshKnownIds() is callable on an empty cache without side effects.
  SyncCredentialsStore store(m_services);
  store.refreshKnownIds();
  QVERIFY(!store.hasCredentials(QStringLiteral("nb_w2t0_anything")));
#else
  // Seed via a first store instance.
  const QString notebookId = QStringLiteral("nb_w2t0_refresh");
  const QString pat = QStringLiteral("test_pat_12345");
  {
    SyncCredentialsStore seedStore(m_services);
    QSignalSpy storedSpy(&seedStore, &SyncCredentialsStore::credentialsStored);
    QSignalSpy errSpy(&seedStore, &SyncCredentialsStore::credentialsStoreError);
    seedStore.storeCredentials(notebookId, {QStringLiteral("git"), QString(), pat});
    int which = waitForEither(storedSpy, errSpy, 5000);
    if (which == 2) {
      const QString errMsg = errSpy.first().at(1).toString();
      QSKIP(qPrintable(QStringLiteral("OS keychain backend not usable in this test environment: %1")
                           .arg(errMsg)));
    }
    QCOMPARE(which, 1);
    // NOTE: do NOT clean up here — the second half of this test must be able to
    // retrieve the credential via freshStore. freshGuard tracks this id below
    // and cleans it up at end-of-test.
  }

  // A FRESH store instance has an empty cache.
  SyncCredentialsStore freshStore(m_services);
  tests::KeychainGuard freshGuard(&freshStore);
  // Manually register the seed-written id (freshStore never called storeCredentials,
  // so the credentialsStored signal never fires on freshStore's store).
  freshGuard.track(notebookId);
  QVERIFY2(!freshStore.hasCredentials(notebookId),
           "Fresh store must start with empty cache (no enumerate API)");

  // refreshKnownIds() is documented as a best-effort no-op against QtKeychain.
  // After calling it the cache must still be empty — and the call must not
  // crash.
  freshStore.refreshKnownIds();
  QVERIFY2(!freshStore.hasCredentials(notebookId),
           "refreshKnownIds() against QtKeychain is a no-op; cache remains empty");

  // Indirect cache population: a successful retrieveCredentials probe causes
  // the cache to gain the id, demonstrating the cache-from-signals invariant
  // that refresh hooks rely on.
  QSignalSpy retrievedSpy(&freshStore, &SyncCredentialsStore::credentialsRetrieved);
  QSignalSpy errSpy(&freshStore, &SyncCredentialsStore::credentialsError);
  freshStore.retrieveCredentials(notebookId);
  int which = waitForEither(retrievedSpy, errSpy, 5000);
  QCOMPARE(which, 1);
  QVERIFY2(freshStore.hasCredentials(notebookId),
           "Cache must be populated by successful credentialsRetrieved signal");

  // POST-test cleanup: both stores' writes are cleaned up by their guards
  freshGuard.cleanup();
#endif
}

void TestSyncCredentialsStore::testStoreSafeFromWorkerThread() {
#ifndef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("VNOTE_KEYCHAIN_AVAILABLE not set: cross-thread parenting warning "
        "only fires in the keychain branch; the #else branch already "
        "self-marshals");
#else
  SyncCredentialsStore store(m_services);
  tests::KeychainGuard guard(&store);
  const QString notebookId = QStringLiteral("nb_t4_worker_store");
  const QString pat = QStringLiteral("ghp_workerThreadCheck");

  // Install message handler BEFORE the worker fires.
  g_logCapture.clear();
  g_previousHandler = qInstallMessageHandler(captureMessageHandler);

  QThread *worker = QThread::create([&store, notebookId, pat]() {
    store.storeCredentials(notebookId, {QStringLiteral("git"), QString(), pat});
  });
  QSignalSpy doneSpy(&store, &SyncCredentialsStore::credentialsStored);
  QSignalSpy errSpy(&store, &SyncCredentialsStore::credentialsStoreError);
  worker->start();
  QVERIFY2(worker->wait(2000), "worker thread must finish (the call returns immediately "
                               "after the QueuedConnection invoke)");

  // Wait for the marshaled call to actually execute on the GUI thread
  // and then for QtKeychain's async response.
  const int which = waitForEither(doneSpy, errSpy, 5000);
  QVERIFY2(which != 0, "marshaled storeCredentials must complete");

  // Drain any final pending events so the message handler catches
  // everything before we restore.
  QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

  qInstallMessageHandler(g_previousHandler);
  g_previousHandler = nullptr;

  worker->deleteLater();

  QVERIFY2(!g_logCapture.contains(
               QStringLiteral("Cannot create children for a parent that is in a different thread")),
           qPrintable(QStringLiteral("Cross-thread QObject parenting warning leaked.\n"
                                     "Captured log:\n%1")
                          .arg(g_logCapture)));

  guard.cleanup();
#endif
}

void TestSyncCredentialsStore::testRetrieveSafeFromWorkerThread() {
#ifndef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("VNOTE_KEYCHAIN_AVAILABLE not set: cross-thread parenting warning "
        "only fires in the keychain branch; the #else branch already "
        "self-marshals");
#else
  SyncCredentialsStore store(m_services);
  tests::KeychainGuard guard(&store);
  const QString notebookId = QStringLiteral("nb_t4_worker_retrieve");

  // Install message handler BEFORE the worker fires.
  g_logCapture.clear();
  g_previousHandler = qInstallMessageHandler(captureMessageHandler);

  QThread *worker =
      QThread::create([&store, notebookId]() { store.retrieveCredentials(notebookId); });
  QSignalSpy doneSpy(&store, &SyncCredentialsStore::credentialsRetrieved);
  QSignalSpy errSpy(&store, &SyncCredentialsStore::credentialsError);
  worker->start();
  QVERIFY2(worker->wait(2000), "worker thread must finish (the call returns immediately "
                               "after the QueuedConnection invoke)");

  // Wait for the marshaled call to actually execute on the GUI thread
  // and then for QtKeychain's async response.
  const int which = waitForEither(doneSpy, errSpy, 5000);
  QVERIFY2(which != 0, "marshaled retrieveCredentials must complete");

  // Drain any final pending events so the message handler catches
  // everything before we restore.
  QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

  qInstallMessageHandler(g_previousHandler);
  g_previousHandler = nullptr;

  worker->deleteLater();

  QVERIFY2(!g_logCapture.contains(
               QStringLiteral("Cannot create children for a parent that is in a different thread")),
           qPrintable(QStringLiteral("Cross-thread QObject parenting warning leaked.\n"
                                     "Captured log:\n%1")
                          .arg(g_logCapture)));

  guard.cleanup();
#endif
}

void TestSyncCredentialsStore::testDeleteSafeFromWorkerThread() {
#ifndef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("VNOTE_KEYCHAIN_AVAILABLE not set: cross-thread parenting warning "
        "only fires in the keychain branch; the #else branch already "
        "self-marshals");
#else
  SyncCredentialsStore store(m_services);
  tests::KeychainGuard guard(&store);
  const QString notebookId = QStringLiteral("nb_t4_worker_delete");

  // Install message handler BEFORE the worker fires.
  g_logCapture.clear();
  g_previousHandler = qInstallMessageHandler(captureMessageHandler);

  QThread *worker =
      QThread::create([&store, notebookId]() { store.deleteCredentials(notebookId); });
  QSignalSpy doneSpy(&store, &SyncCredentialsStore::credentialsDeleted);
  QSignalSpy errSpy(&store, &SyncCredentialsStore::credentialsError);
  worker->start();
  QVERIFY2(worker->wait(2000), "worker thread must finish (the call returns immediately "
                               "after the QueuedConnection invoke)");

  // Wait for the marshaled call to actually execute on the GUI thread
  // and then for QtKeychain's async response.
  const int which = waitForEither(doneSpy, errSpy, 5000);
  QVERIFY2(which != 0, "marshaled deleteCredentials must complete");

  // Drain any final pending events so the message handler catches
  // everything before we restore.
  QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

  qInstallMessageHandler(g_previousHandler);
  g_previousHandler = nullptr;

  worker->deleteLater();

  QVERIFY2(!g_logCapture.contains(
               QStringLiteral("Cannot create children for a parent that is in a different thread")),
           qPrintable(QStringLiteral("Cross-thread QObject parenting warning leaked.\n"
                                     "Captured log:\n%1")
                          .arg(g_logCapture)));

  guard.cleanup();
#endif
}

} // namespace tests

VNOTE_KEYCHAIN_TEST_MAIN(tests::TestSyncCredentialsStore)
#include "test_synccredentialsstore.moc"
