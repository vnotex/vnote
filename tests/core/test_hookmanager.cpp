#include <QtTest>

#include <core/hookcontext.h>
#include <core/services/hookmanager.h>

namespace tests {

class TestHookManager : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  // HookContext tests
  void testHookContextDefaultConstruction();
  void testHookContextCancel();
  void testHookContextStopPropagation();
  void testHookContextMetadata();

  // Action tests
  void testAddAction();
  void testRemoveAction();
  void testDoActionNotCancelled();
  void testDoActionCancelled();
  void testDoActionStopPropagation();
  void testActionPriority();
  void testActionErrorHandling();

  // Filter tests
  void testAddFilter();
  void testRemoveFilter();
  void testApplyFiltersChain();
  void testFilterPriority();
  void testFilterErrorHandling();

  // Introspection tests
  void testRegisteredHooks();
  void testCallbackCount();
  void testHasCallbacks();

  // Edge cases
  void testRecursionGuard();
  void testNullCallback();
  void testEmptyHook();

private:
  vnotex::HookManager *m_hookManager = nullptr;
};

void TestHookManager::initTestCase() { m_hookManager = new vnotex::HookManager(this); }

void TestHookManager::cleanupTestCase() {
  delete m_hookManager;
  m_hookManager = nullptr;
}

// ===== HookContext Tests =====

void TestHookManager::testHookContextDefaultConstruction() {
  vnotex::HookContext ctx;
  QVERIFY(!ctx.isCancelled());
  QVERIFY(!ctx.isPropagationStopped());
  QVERIFY(ctx.hookName().isEmpty());
  QVERIFY(ctx.metadata().isEmpty());
}

void TestHookManager::testHookContextCancel() {
  vnotex::HookContext ctx("test.hook");
  QVERIFY(!ctx.isCancelled());
  ctx.cancel();
  QVERIFY(ctx.isCancelled());
}

void TestHookManager::testHookContextStopPropagation() {
  vnotex::HookContext ctx("test.hook");
  QVERIFY(!ctx.isPropagationStopped());
  ctx.stopPropagation();
  QVERIFY(ctx.isPropagationStopped());
}

void TestHookManager::testHookContextMetadata() {
  vnotex::HookContext ctx("test.hook");
  ctx.setMetadata("key1", "value1");
  ctx.setMetadata("key2", 42);

  QCOMPARE(ctx.getMetadata("key1").toString(), QString("value1"));
  QCOMPARE(ctx.getMetadata("key2").toInt(), 42);
  QCOMPARE(ctx.getMetadata("nonexistent", "default").toString(), QString("default"));
  QCOMPARE(ctx.metadata().size(), 2);
}

// ===== Action Tests =====

void TestHookManager::testAddAction() {
  int id = m_hookManager->addAction("test.action", [](vnotex::HookContext &, const QVariantMap &) {
    // Empty callback
  });
  QVERIFY(id > 0);
  QCOMPARE(m_hookManager->actionCount("test.action"), 1);
  m_hookManager->removeAction(id);
}

void TestHookManager::testRemoveAction() {
  int id = m_hookManager->addAction("test.remove", [](vnotex::HookContext &, const QVariantMap &) {
    // Empty callback
  });
  QCOMPARE(m_hookManager->actionCount("test.remove"), 1);
  QVERIFY(m_hookManager->removeAction(id));
  QCOMPARE(m_hookManager->actionCount("test.remove"), 0);
  QVERIFY(!m_hookManager->removeAction(id)); // Already removed
}

void TestHookManager::testDoActionNotCancelled() {
  bool called = false;
  int id = m_hookManager->addAction(
      "test.notcancelled", [&called](vnotex::HookContext &, const QVariantMap &) { called = true; });

  bool cancelled = m_hookManager->doAction("test.notcancelled");
  QVERIFY(called);
  QVERIFY(!cancelled);
  m_hookManager->removeAction(id);
}

void TestHookManager::testDoActionCancelled() {
  int id = m_hookManager->addAction(
      "test.cancelled", [](vnotex::HookContext &ctx, const QVariantMap &) { ctx.cancel(); });

  bool cancelled = m_hookManager->doAction("test.cancelled");
  QVERIFY(cancelled);
  m_hookManager->removeAction(id);
}

void TestHookManager::testDoActionStopPropagation() {
  int callCount = 0;
  int id1 = m_hookManager->addAction("test.stopprop",
                                     [&callCount](vnotex::HookContext &ctx, const QVariantMap &) {
                                       ++callCount;
                                       ctx.stopPropagation();
                                     });
  int id2 = m_hookManager->addAction(
      "test.stopprop", [&callCount](vnotex::HookContext &, const QVariantMap &) { ++callCount; });

  m_hookManager->doAction("test.stopprop");
  QCOMPARE(callCount, 1); // Only first callback should run
  m_hookManager->removeAction(id1);
  m_hookManager->removeAction(id2);
}

void TestHookManager::testActionPriority() {
  QStringList order;
  int id1 = m_hookManager->addAction(
      "test.priority", [&order](vnotex::HookContext &, const QVariantMap &) { order << "second"; },
      20);
  int id2 = m_hookManager->addAction(
      "test.priority", [&order](vnotex::HookContext &, const QVariantMap &) { order << "first"; },
      10);
  int id3 = m_hookManager->addAction(
      "test.priority", [&order](vnotex::HookContext &, const QVariantMap &) { order << "third"; },
      30);

  m_hookManager->doAction("test.priority");
  QCOMPARE(order, QStringList({"first", "second", "third"}));

  m_hookManager->removeAction(id1);
  m_hookManager->removeAction(id2);
  m_hookManager->removeAction(id3);
}

void TestHookManager::testActionErrorHandling() {
  bool errorEmitted = false;
  connect(m_hookManager, &vnotex::HookManager::actionError, this,
          [&errorEmitted](const QString &, const QString &) { errorEmitted = true; });

  int id = m_hookManager->addAction("test.error", [](vnotex::HookContext &, const QVariantMap &) {
    throw std::runtime_error("Test error");
  });

  // Should not throw, should emit signal
  m_hookManager->doAction("test.error");
  QVERIFY(errorEmitted);
  m_hookManager->removeAction(id);
}

// ===== Filter Tests =====

void TestHookManager::testAddFilter() {
  int id = m_hookManager->addFilter("test.filter",
                                    [](const QVariant &value, const QVariantMap &) { return value; });
  QVERIFY(id > 0);
  QCOMPARE(m_hookManager->filterCount("test.filter"), 1);
  m_hookManager->removeFilter(id);
}

void TestHookManager::testRemoveFilter() {
  int id = m_hookManager->addFilter("test.removefilter",
                                    [](const QVariant &value, const QVariantMap &) { return value; });
  QCOMPARE(m_hookManager->filterCount("test.removefilter"), 1);
  QVERIFY(m_hookManager->removeFilter(id));
  QCOMPARE(m_hookManager->filterCount("test.removefilter"), 0);
  QVERIFY(!m_hookManager->removeFilter(id)); // Already removed
}

void TestHookManager::testApplyFiltersChain() {
  int id1 = m_hookManager->addFilter("test.chain", [](const QVariant &value, const QVariantMap &) {
    return value.toInt() + 1;
  });
  int id2 = m_hookManager->addFilter("test.chain", [](const QVariant &value, const QVariantMap &) {
    return value.toInt() * 2;
  });

  QVariant result = m_hookManager->applyFilters("test.chain", 5);
  QCOMPARE(result.toInt(), 12); // (5 + 1) * 2 = 12

  m_hookManager->removeFilter(id1);
  m_hookManager->removeFilter(id2);
}

void TestHookManager::testFilterPriority() {
  // Lower priority runs first
  int id1 = m_hookManager->addFilter(
      "test.filterprio",
      [](const QVariant &value, const QVariantMap &) {
        return value.toString() + "B"; // Priority 20, runs second
      },
      20);
  int id2 = m_hookManager->addFilter(
      "test.filterprio",
      [](const QVariant &value, const QVariantMap &) {
        return value.toString() + "A"; // Priority 10, runs first
      },
      10);

  QVariant result = m_hookManager->applyFilters("test.filterprio", QString(""));
  QCOMPARE(result.toString(), QString("AB"));

  m_hookManager->removeFilter(id1);
  m_hookManager->removeFilter(id2);
}

void TestHookManager::testFilterErrorHandling() {
  bool errorEmitted = false;
  connect(m_hookManager, &vnotex::HookManager::filterError, this,
          [&errorEmitted](const QString &, const QString &) { errorEmitted = true; });

  int id = m_hookManager->addFilter("test.filtererror", [](const QVariant &, const QVariantMap &) {
    throw std::runtime_error("Filter error");
    return QVariant();
  });

  // Should not throw, should return original value
  QVariant result = m_hookManager->applyFilters("test.filtererror", 42);
  QVERIFY(errorEmitted);
  QCOMPARE(result.toInt(), 42); // Original value preserved on error
  m_hookManager->removeFilter(id);
}

// ===== Introspection Tests =====

void TestHookManager::testRegisteredHooks() {
  int id1 = m_hookManager->addAction("hook.a", [](vnotex::HookContext &, const QVariantMap &) {});
  int id2 = m_hookManager->addFilter("hook.b", [](const QVariant &v, const QVariantMap &) { return v; });

  QStringList hooks = m_hookManager->registeredHooks();
  QVERIFY(hooks.contains("hook.a"));
  QVERIFY(hooks.contains("hook.b"));

  m_hookManager->removeAction(id1);
  m_hookManager->removeFilter(id2);
}

void TestHookManager::testCallbackCount() {
  int id1 = m_hookManager->addAction("hook.count", [](vnotex::HookContext &, const QVariantMap &) {});
  int id2 =
      m_hookManager->addFilter("hook.count", [](const QVariant &v, const QVariantMap &) { return v; });

  QCOMPARE(m_hookManager->callbackCount("hook.count"), 2);
  QCOMPARE(m_hookManager->actionCount("hook.count"), 1);
  QCOMPARE(m_hookManager->filterCount("hook.count"), 1);

  m_hookManager->removeAction(id1);
  m_hookManager->removeFilter(id2);
}

void TestHookManager::testHasCallbacks() {
  QVERIFY(!m_hookManager->hasCallbacks("nonexistent.hook"));

  int id = m_hookManager->addAction("exists.hook", [](vnotex::HookContext &, const QVariantMap &) {});
  QVERIFY(m_hookManager->hasCallbacks("exists.hook"));
  m_hookManager->removeAction(id);
  QVERIFY(!m_hookManager->hasCallbacks("exists.hook"));
}

// ===== Edge Cases =====

void TestHookManager::testRecursionGuard() {
  // Create a hook that triggers itself
  bool errorEmitted = false;
  connect(m_hookManager, &vnotex::HookManager::actionError, this,
          [&errorEmitted](const QString &, const QString &error) {
            if (error.contains("recursion")) {
              errorEmitted = true;
            }
          });

  int id = m_hookManager->addAction(
      "recursive.hook", [this](vnotex::HookContext &, const QVariantMap &) {
        m_hookManager->doAction("recursive.hook"); // Recursive call
      });

  // Should not crash, should hit recursion limit
  m_hookManager->doAction("recursive.hook");
  QVERIFY(errorEmitted);
  m_hookManager->removeAction(id);
}

void TestHookManager::testNullCallback() {
  int id = m_hookManager->addAction("null.action", nullptr);
  QCOMPARE(id, -1); // Should return -1 for null callback
  QCOMPARE(m_hookManager->actionCount("null.action"), 0);

  id = m_hookManager->addFilter("null.filter", nullptr);
  QCOMPARE(id, -1);
  QCOMPARE(m_hookManager->filterCount("null.filter"), 0);
}

void TestHookManager::testEmptyHook() {
  // doAction on non-existent hook should not crash and return false
  bool cancelled = m_hookManager->doAction("nonexistent.hook");
  QVERIFY(!cancelled);

  // applyFilters on non-existent hook should return original value
  QVariant result = m_hookManager->applyFilters("nonexistent.filter", 42);
  QCOMPARE(result.toInt(), 42);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestHookManager)
#include "test_hookmanager.moc"
