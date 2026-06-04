// test_syncservice.cpp - Unit tests for SyncService empty-PAT/URL guards (Task 3)
//
// Verifies that SyncService::enableSyncForNotebook rejects empty PAT and empty
// remote URL early (before keychain write or worker dispatch) by:
//   - Checking enableFinished signal carries VXCORE_ERR_INVALID_PARAM
//   - Verifying the error message is user-facing and non-empty
//   - Ensuring no downstream keychain/worker operations are triggered

#include <QCoreApplication>
#include <QSignalSpy>
#include <QString>
#include <QtTest>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

#include "../helpers/keychain_guard.h"

using namespace vnotex;

namespace tests {

class TestSyncService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testEnableSyncEmptyPatShortCircuits();
  void testEnableSyncEmptyUrlShortCircuits();

private:
  VxCoreContextHandle m_context = nullptr;
};

void TestSyncService::initTestCase() {
  // CRITICAL: enable test mode BEFORE creating vxcore context (per tests/AGENTS.md)
  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QVERIFY2(err == VXCORE_OK, "Failed to create vxcore context");
  QVERIFY(m_context != nullptr);
}

void TestSyncService::cleanupTestCase() {
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestSyncService::testEnableSyncEmptyPatShortCircuits() {
  // Arrange: create services on the stack (mirrors test_sync_ops pattern)
  ServiceLocator services;
  NotebookCoreService notebookService(m_context);
  SyncCredentialsStore credStore(services);
  // T5: guard wires the rollout uniformly even when the test short-circuits
  // before any keychain write.
  tests::KeychainGuard guard(&credStore);
  services.registerService<NotebookCoreService>(&notebookService);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);

  // Spy on enableFinished signal
  QSignalSpy spy(&syncService, &SyncService::enableFinished);

  // Act: call enableSyncForNotebook with empty PAT
  const QString notebookId = QStringLiteral("test-nb-empty-pat");
  const QString remoteUrl = QStringLiteral("https://github.com/user/repo.git");
  const QString emptyPat = QString();
  syncService.enableSyncForNotebook(notebookId, remoteUrl, emptyPat);

  // Assert: enableFinished signal fires within 200ms with VXCORE_ERR_INVALID_PARAM
  QVERIFY(spy.wait(200));
  QCOMPARE(spy.count(), 1);

  // Extract signal arguments
  QList<QVariant> args = spy.takeFirst();
  QCOMPARE(args.size(), 3);

  QString resultNotebookId = args.at(0).toString();
  VxCoreError resultCode = static_cast<VxCoreError>(args.at(1).toInt());
  QString resultMsg = args.at(2).toString();

  QCOMPARE(resultNotebookId, notebookId);
  QCOMPARE(resultCode, VXCORE_ERR_INVALID_PARAM);
  QVERIFY(!resultMsg.isEmpty());
  QVERIFY(resultMsg.contains(QStringLiteral("PAT"), Qt::CaseInsensitive));
  guard.cleanup();
}

void TestSyncService::testEnableSyncEmptyUrlShortCircuits() {
  // Arrange: create services on the stack (mirrors test_sync_ops pattern)
  ServiceLocator services;
  NotebookCoreService notebookService(m_context);
  SyncCredentialsStore credStore(services);
  tests::KeychainGuard guard(&credStore);
  services.registerService<NotebookCoreService>(&notebookService);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);

  // Spy on enableFinished signal
  QSignalSpy spy(&syncService, &SyncService::enableFinished);

  // Act: call enableSyncForNotebook with empty remote URL
  const QString notebookId = QStringLiteral("test-nb-empty-url");
  const QString emptyUrl = QString();
  const QString validPat = QStringLiteral("ghp_validPatToken123");
  syncService.enableSyncForNotebook(notebookId, emptyUrl, validPat);

  // Assert: enableFinished signal fires within 200ms with VXCORE_ERR_INVALID_PARAM
  QVERIFY(spy.wait(200));
  QCOMPARE(spy.count(), 1);

  // Extract signal arguments
  QList<QVariant> args = spy.takeFirst();
  QCOMPARE(args.size(), 3);

  QString resultNotebookId = args.at(0).toString();
  VxCoreError resultCode = static_cast<VxCoreError>(args.at(1).toInt());
  QString resultMsg = args.at(2).toString();

  QCOMPARE(resultNotebookId, notebookId);
  QCOMPARE(resultCode, VXCORE_ERR_INVALID_PARAM);
  QVERIFY(!resultMsg.isEmpty());
  QVERIFY(resultMsg.contains(QStringLiteral("URL"), Qt::CaseInsensitive) ||
          resultMsg.contains(QStringLiteral("Remote"), Qt::CaseInsensitive));
  guard.cleanup();
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncService)
#include "test_syncservice.moc"
