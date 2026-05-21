#include <QtTest>

#include <QThread>

#include <core/services/syncworkqueuemanager.h>

namespace tests {

class TestSyncWorkQueueCapCoalesce : public QObject {
  Q_OBJECT

private slots:
  void setMaxDepthDefault();
  void setMaxDepthCustomValue();
  void capEnforcement();
  void coalesceWithSameKey();
  void coalesceWithDistinctKeys();
  void coalesceWithEmptyKey();
  void coalescePrecedenceOverCap();
};

void TestSyncWorkQueueCapCoalesce::setMaxDepthDefault() {
  vnotex::SyncWorkQueueManager mgr;
  // Just test that default is 4
  if (mgr.maxDepth() != 4) {
    QFAIL("Default maxDepth should be 4");
  }
}

void TestSyncWorkQueueCapCoalesce::setMaxDepthCustomValue() {
  vnotex::SyncWorkQueueManager mgr;
  mgr.setMaxDepth(8);
  QCOMPARE(mgr.maxDepth(), 8);
}

void TestSyncWorkQueueCapCoalesce::capEnforcement() {
  // Simpler test: just verify cap is enforced
  vnotex::SyncWorkQueueManager mgr;
  mgr.setMaxDepth(2);
  const QString id = QStringLiteral("test-notebook");

  // 1st and 2nd enqueue should succeed (reach cap=2)
  auto result1 = mgr.enqueue(id, []() {});
  auto result2 = mgr.enqueue(id, []() {});
  auto result3 = mgr.enqueue(id, []() {});

  QCOMPARE(result1, vnotex::SyncWorkQueueManager::EnqueueResult::Accepted);
  QCOMPARE(result2, vnotex::SyncWorkQueueManager::EnqueueResult::Accepted);
  QCOMPARE(result3, vnotex::SyncWorkQueueManager::EnqueueResult::QueueFull);

  // Let pending items drain
  mgr.shutdown(5000);
}

void TestSyncWorkQueueCapCoalesce::coalesceWithSameKey() {
  // 3 items with same coalesceKey="trigger":
  // 1st Accepted, 2nd+3rd Coalesced.
  vnotex::SyncWorkQueueManager mgr;
  const QString id = QStringLiteral("test-notebook");

  // Simple logic: just test the results without worrying about timing
  auto result1 = mgr.enqueue(id, []() { QThread::msleep(100); }, QStringLiteral("trigger"));
  auto result2 = mgr.enqueue(id, []() {}, QStringLiteral("trigger"));
  auto result3 = mgr.enqueue(id, []() {}, QStringLiteral("trigger"));

  // The test expectation: 1st accepted, others coalesced
  if (result1 != vnotex::SyncWorkQueueManager::EnqueueResult::Accepted) {
    QFAIL(qPrintable(QString("Expected Accepted, got result1")));
  }
  if (result2 != vnotex::SyncWorkQueueManager::EnqueueResult::Coalesced) {
    QFAIL(qPrintable(QString("Expected Coalesced, got result2=%1").arg(static_cast<int>(result2))));
  }
  if (result3 != vnotex::SyncWorkQueueManager::EnqueueResult::Coalesced) {
    QFAIL(qPrintable(QString("Expected Coalesced, got result3=%1").arg(static_cast<int>(result3))));
  }

  mgr.shutdown(5000);
}

void TestSyncWorkQueueCapCoalesce::coalesceWithDistinctKeys() {
  // 3 items with distinct coalesceKeys ("a", "b", "c"):
  // All Accepted (no coalescing).
  vnotex::SyncWorkQueueManager mgr;
  const QString id = QStringLiteral("test-notebook");

  auto result1 = mgr.enqueue(id, []() {}, QStringLiteral("a"));
  auto result2 = mgr.enqueue(id, []() {}, QStringLiteral("b"));
  auto result3 = mgr.enqueue(id, []() {}, QStringLiteral("c"));

  QCOMPARE(result1, vnotex::SyncWorkQueueManager::EnqueueResult::Accepted);
  QCOMPARE(result2, vnotex::SyncWorkQueueManager::EnqueueResult::Accepted);
  QCOMPARE(result3, vnotex::SyncWorkQueueManager::EnqueueResult::Accepted);

  mgr.shutdown(5000);
}

void TestSyncWorkQueueCapCoalesce::coalesceWithEmptyKey() {
  // 3 items with empty coalesceKey "" (no coalescing):
  // All Accepted.
  vnotex::SyncWorkQueueManager mgr;
  const QString id = QStringLiteral("test-notebook");

  auto result1 = mgr.enqueue(id, []() {}, QString());
  auto result2 = mgr.enqueue(id, []() {}, QString());
  auto result3 = mgr.enqueue(id, []() {}, QString());

  QCOMPARE(result1, vnotex::SyncWorkQueueManager::EnqueueResult::Accepted);
  QCOMPARE(result2, vnotex::SyncWorkQueueManager::EnqueueResult::Accepted);
  QCOMPARE(result3, vnotex::SyncWorkQueueManager::EnqueueResult::Accepted);

  mgr.shutdown(5000);
}

void TestSyncWorkQueueCapCoalesce::coalescePrecedenceOverCap() {
  // 4 items with coalesceKey="trigger" + cap=2:
  // 1st Accepted, 2nd-4th Coalesced (NOT QueueFull).
  vnotex::SyncWorkQueueManager mgr;
  mgr.setMaxDepth(2);
  const QString id = QStringLiteral("test-notebook");

  // First work with coalesce key
  auto result1 = mgr.enqueue(id, []() {}, QStringLiteral("trigger"));
  QCOMPARE(result1, vnotex::SyncWorkQueueManager::EnqueueResult::Accepted);

  // 2nd-4th with same key should all be coalesced despite cap
  auto result2 = mgr.enqueue(id, []() {}, QStringLiteral("trigger"));
  QCOMPARE(result2, vnotex::SyncWorkQueueManager::EnqueueResult::Coalesced);

  auto result3 = mgr.enqueue(id, []() {}, QStringLiteral("trigger"));
  QCOMPARE(result3, vnotex::SyncWorkQueueManager::EnqueueResult::Coalesced);

  auto result4 = mgr.enqueue(id, []() {}, QStringLiteral("trigger"));
  QCOMPARE(result4, vnotex::SyncWorkQueueManager::EnqueueResult::Coalesced);

  mgr.shutdown(5000);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncWorkQueueCapCoalesce)

#include "test_sync_workqueue_cap_coalesce.moc"
