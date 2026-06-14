// test_hooknames_reorder.cpp - Unit tests for NodeReorderEvent hook constants and typed doAction
#include <QtTest>

#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

namespace tests {

class TestHookNamesReorder : public QObject {
  Q_OBJECT

private slots:
  void testNodeBeforeReorderConstant();
  void testNodeAfterReorderConstant();
  void testNodeReorderEventConstruction();
  void testNodeReorderEventDoAction();
};

void TestHookNamesReorder::testNodeBeforeReorderConstant() {
  QCOMPARE(QString(HookNames::NodeBeforeReorder), QString("vnote.node.before_reorder"));
}

void TestHookNamesReorder::testNodeAfterReorderConstant() {
  QCOMPARE(QString(HookNames::NodeAfterReorder), QString("vnote.node.after_reorder"));
}

void TestHookNamesReorder::testNodeReorderEventConstruction() {
  NodeReorderEvent event;
  event.notebookId = "nb-id-1";
  event.folderRelPath = "folder/path";
  event.previousFolderOrder = QStringList() << "a" << "b" << "c";
  event.previousFileOrder = QStringList() << "x.md" << "y.md";
  event.newFolderOrder = QStringList() << "c" << "a" << "b";
  event.newFileOrder = QStringList() << "y.md" << "x.md";

  // Verify fields were set correctly
  QCOMPARE(event.notebookId, QString("nb-id-1"));
  QCOMPARE(event.folderRelPath, QString("folder/path"));
  QCOMPARE(event.previousFolderOrder.size(), 3);
  QCOMPARE(event.previousFolderOrder[0], QString("a"));
  QCOMPARE(event.previousFileOrder.size(), 2);
  QCOMPARE(event.previousFileOrder[0], QString("x.md"));
  QCOMPARE(event.newFolderOrder.size(), 3);
  QCOMPARE(event.newFolderOrder[0], QString("c"));
  QCOMPARE(event.newFileOrder.size(), 2);
  QCOMPARE(event.newFileOrder[0], QString("y.md"));

  // Verify round-trip conversion (to/from QVariantMap)
  QVariantMap varMap = event.toVariantMap();
  NodeReorderEvent restored = NodeReorderEvent::fromVariantMap(varMap);

  QCOMPARE(restored.notebookId, event.notebookId);
  QCOMPARE(restored.folderRelPath, event.folderRelPath);
  QCOMPARE(restored.previousFolderOrder, event.previousFolderOrder);
  QCOMPARE(restored.previousFileOrder, event.previousFileOrder);
  QCOMPARE(restored.newFolderOrder, event.newFolderOrder);
  QCOMPARE(restored.newFileOrder, event.newFileOrder);
}

void TestHookNamesReorder::testNodeReorderEventDoAction() {
  HookManager hm;

  NodeReorderEvent event;
  event.notebookId = "test-notebook";
  event.folderRelPath = "";
  event.previousFolderOrder = QStringList() << "old-1" << "old-2";
  event.newFolderOrder = QStringList() << "old-2" << "old-1";

  // Call doAction with no callbacks registered
  // Expected: returns false (not cancelled, because no callbacks are registered)
  bool isCancelled = hm.doAction(HookNames::NodeBeforeReorder, event);

  QVERIFY(!isCancelled);

  // Register a callback that doesn't cancel
  int callCount = 0;
  hm.addAction(
      HookNames::NodeBeforeReorder,
      [&callCount](HookContext &, const QVariantMap &) { ++callCount; }, 10);

  // Call doAction again - callback should fire
  isCancelled = hm.doAction(HookNames::NodeBeforeReorder, event);

  QCOMPARE(callCount, 1);
  QVERIFY(!isCancelled); // Callback didn't cancel

  // Register a callback that cancels
  int cancelCallCount = 0;
  hm.addAction(
      HookNames::NodeBeforeReorder,
      [&cancelCallCount](HookContext &p_ctx, const QVariantMap &) {
        ++cancelCallCount;
        p_ctx.cancel();
      },
      5); // Higher priority (lower number = earlier execution)

  // Call doAction - should be cancelled
  isCancelled = hm.doAction(HookNames::NodeBeforeReorder, event);

  QCOMPARE(cancelCallCount, 1);
  QVERIFY(isCancelled); // Callback cancelled the action
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestHookNamesReorder)
#include "test_hooknames_reorder.moc"
