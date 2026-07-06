#include <QtTest>

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

  // Force an in-flight item so the pool worker never drains the pending queue
  // mid-test. Otherwise the worker can dequeue the head item between the
  // synchronous enqueue() calls below, so the queue never reaches the cap and
  // result3 flakes to Accepted instead of QueueFull (observed on CI).
  mgr.testForceInFlight(id, true);

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

  // Force an in-flight item so the pool worker never drains the pending queue
  // mid-test; otherwise the first item can be dequeued before the 2nd/3rd
  // enqueue, leaving nothing to coalesce against (races under CI load).
  mgr.testForceInFlight(id, true);

  auto result1 = mgr.enqueue(id, []() {}, QStringLiteral("trigger"));
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
  // Coalescing is checked BEFORE the cap: a same-key enqueue coalesces even
  // when the pending queue is already AT maxDepth, whereas a distinct key at
  // the same moment is rejected with QueueFull.
  vnotex::SyncWorkQueueManager mgr;
  mgr.setMaxDepth(1);
  const QString id = QStringLiteral("test-notebook");

  // Force an in-flight item so the pool worker never drains the pending queue
  // mid-test. Without this the worker can dequeue the first "trigger" item
  // between the synchronous enqueue() calls, leaving nothing to coalesce
  // against and no at-cap queue to test precedence against (the CI flake).
  mgr.testForceInFlight(id, true);

  // One pending "trigger" item fills the queue to cap (maxDepth=1).
  auto result1 = mgr.enqueue(id, []() {}, QStringLiteral("trigger"));
  QCOMPARE(result1, vnotex::SyncWorkQueueManager::EnqueueResult::Accepted);

  // Same key while the queue is AT cap → Coalesced (coalesce beats the cap).
  auto result2 = mgr.enqueue(id, []() {}, QStringLiteral("trigger"));
  QCOMPARE(result2, vnotex::SyncWorkQueueManager::EnqueueResult::Coalesced);

  auto result3 = mgr.enqueue(id, []() {}, QStringLiteral("trigger"));
  QCOMPARE(result3, vnotex::SyncWorkQueueManager::EnqueueResult::Coalesced);

  auto result4 = mgr.enqueue(id, []() {}, QStringLiteral("trigger"));
  QCOMPARE(result4, vnotex::SyncWorkQueueManager::EnqueueResult::Coalesced);

  // A DISTINCT key at the same at-cap moment has nothing to coalesce with and
  // is rejected by the cap → QueueFull. This contrast is what "precedence"
  // means: coalesce short-circuits the cap only for a matching key.
  auto resultOther = mgr.enqueue(id, []() {}, QStringLiteral("other"));
  QCOMPARE(resultOther, vnotex::SyncWorkQueueManager::EnqueueResult::QueueFull);

  mgr.shutdown(5000);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncWorkQueueCapCoalesce)

#include "test_sync_workqueue_cap_coalesce.moc"
