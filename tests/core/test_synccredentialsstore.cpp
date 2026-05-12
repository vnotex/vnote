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
#include <QSignalSpy>
#include <QString>
#include <QtTest>

#include <core/servicelocator.h>
#include <core/services/synccredentialsstore.h>
#include <vxcore/vxcore.h>

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
  void keychainUnavailableEmitsError();
  void patNotLogged();

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

void TestSyncCredentialsStore::storeRetrieve() {
#ifndef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("VNOTE_KEYCHAIN_AVAILABLE not set: cannot test happy path");
#else
  SyncCredentialsStore store(m_services);
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
  QSignalSpy errorSpy(&store, &SyncCredentialsStore::credentialsError);
  store.storeCredentials(notebookId, pat);
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
  QCOMPARE(retrievedSpy.first().at(1).toString(), pat);

  // Cleanup
  QSignalSpy deletedSpy(&store, &SyncCredentialsStore::credentialsDeleted);
  QSignalSpy errorSpy3(&store, &SyncCredentialsStore::credentialsError);
  store.deleteCredentials(notebookId);
  waitForEither(deletedSpy, errorSpy3, 5000);
#endif
}

void TestSyncCredentialsStore::delete_() {
#ifndef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("VNOTE_KEYCHAIN_AVAILABLE not set: cannot test happy path");
#else
  SyncCredentialsStore store(m_services);
  const QString notebookId = QStringLiteral("nb_t4_deletetest");
  const QString pat = QStringLiteral("ghp_DELETEME");

  // Store first
  QSignalSpy storedSpy(&store, &SyncCredentialsStore::credentialsStored);
  QSignalSpy errSpy1(&store, &SyncCredentialsStore::credentialsError);
  store.storeCredentials(notebookId, pat);
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

  // Subsequent retrieve should produce either an error (key not found) or an
  // empty PAT. Both outcomes confirm the entry is gone.
  QSignalSpy retrievedSpy(&store, &SyncCredentialsStore::credentialsRetrieved);
  QSignalSpy errSpy3(&store, &SyncCredentialsStore::credentialsError);
  store.retrieveCredentials(notebookId);
  which = waitForEither(retrievedSpy, errSpy3, 5000);
  QVERIFY(which != 0);
  if (which == 1) {
    QVERIFY(retrievedSpy.first().at(1).toString().isEmpty());
  }
#endif
}

void TestSyncCredentialsStore::keychainUnavailableEmitsError() {
#ifdef VNOTE_KEYCHAIN_AVAILABLE
  QSKIP("requires VNOTE_USE_KEYCHAIN=OFF build to test fallback path");
#else
  SyncCredentialsStore store(m_services);
  const QString notebookId = QStringLiteral("nb_t4_unavailable");

  QSignalSpy errorSpy(&store, &SyncCredentialsStore::credentialsError);
  store.storeCredentials(notebookId, QStringLiteral("any_pat"));
  QVERIFY(errorSpy.wait(5000));
  QCOMPARE(errorSpy.count(), 1);
  QCOMPARE(errorSpy.first().at(0).toString(), notebookId);
  QVERIFY(
      errorSpy.first().at(1).toString().contains(QStringLiteral("secure-keychain-unavailable")));
#endif
}

void TestSyncCredentialsStore::patNotLogged() {
  // Install handler capturing all log output.
  g_logCapture.clear();
  g_previousHandler = qInstallMessageHandler(captureMessageHandler);

  SyncCredentialsStore store(m_services);
  const QString notebookId = QStringLiteral("nb_t4_logleakcheck");
  const QString uniquePat = QStringLiteral("uniqueLeakCheck1234567890");

  // Roundtrip: store + retrieve + delete. Whatever the outcome (success or
  // error path), the PAT MUST NOT appear in any captured log message.
  {
    QSignalSpy doneSpy(&store, &SyncCredentialsStore::credentialsStored);
    QSignalSpy errSpy(&store, &SyncCredentialsStore::credentialsError);
    store.storeCredentials(notebookId, uniquePat);
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
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncCredentialsStore)
#include "test_synccredentialsstore.moc"
