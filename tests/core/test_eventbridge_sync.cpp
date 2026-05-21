// T17 — EventBridge sync event routing.
//
// Verifies EventBridge correctly translates vxcore sync events into Qt signals:
//   1. sync.should_run  → syncShouldRun(notebookId)
//   2. sync.conflict    → syncConflict(notebookId) AND syncConflictFiles(id, files)
//   3. sync.conflict missing "files" key → syncConflictFiles fires with empty list

#include <QCoreApplication>
#include <QSignalSpy>
#include <QtTest>

#include <nlohmann/json.hpp>

#include <core/services/eventbridge.h>

#include "core/context.h"
#include "core/event_manager.h"
#include "core/event_names.h"

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestEventBridgeSync : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();

  void test_eventbridge_fires_sync_should_run();
  void test_eventbridge_conflict_carries_files();
  void test_eventbridge_conflict_no_files_key();

private:
  static vxcore::EventManager *eventMgr(VxCoreContextHandle p_ctx) {
    return reinterpret_cast<vxcore::VxCoreContext *>(p_ctx)->event_manager.get();
  }
};

void TestEventBridgeSync::initTestCase() { vxcore_set_test_mode(1); }

void TestEventBridgeSync::test_eventbridge_fires_sync_should_run() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create(nullptr, &ctx), VXCORE_OK);
  QVERIFY(ctx);
  {
    EventBridge bridge(ctx);
    QSignalSpy spy(&bridge, &EventBridge::syncShouldRun);

    eventMgr(ctx)->Emit(vxcore::events::kSyncShouldRun, {{"notebookId", "nb-test"}});

    QVERIFY(spy.wait(2000) || spy.count() >= 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("nb-test"));
  }
  vxcore_context_destroy(ctx);
}

void TestEventBridgeSync::test_eventbridge_conflict_carries_files() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create(nullptr, &ctx), VXCORE_OK);
  QVERIFY(ctx);
  {
    EventBridge bridge(ctx);
    QSignalSpy filesSpy(&bridge, &EventBridge::syncConflictFiles);
    QSignalSpy legacySpy(&bridge, &EventBridge::syncConflict);

    nlohmann::json payload = {{"notebookId", "nb-test"},
                              {"files", nlohmann::json::array({"a.md", "b.md"})}};
    eventMgr(ctx)->Emit(vxcore::events::kSyncConflict, payload);

    QVERIFY(filesSpy.wait(2000) || filesSpy.count() >= 1);
    // Drain any other queued events.
    QCoreApplication::processEvents();

    QCOMPARE(legacySpy.count(), 1);
    QCOMPARE(legacySpy.at(0).at(0).toString(), QStringLiteral("nb-test"));

    QCOMPARE(filesSpy.count(), 1);
    QCOMPARE(filesSpy.at(0).at(0).toString(), QStringLiteral("nb-test"));
    const QStringList files = filesSpy.at(0).at(1).toStringList();
    QCOMPARE(files.size(), 2);
    QCOMPARE(files.at(0), QStringLiteral("a.md"));
    QCOMPARE(files.at(1), QStringLiteral("b.md"));
  }
  vxcore_context_destroy(ctx);
}

void TestEventBridgeSync::test_eventbridge_conflict_no_files_key() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create(nullptr, &ctx), VXCORE_OK);
  QVERIFY(ctx);
  {
    EventBridge bridge(ctx);
    QSignalSpy filesSpy(&bridge, &EventBridge::syncConflictFiles);

    eventMgr(ctx)->Emit(vxcore::events::kSyncConflict, {{"notebookId", "nb-test"}});

    QVERIFY(filesSpy.wait(2000) || filesSpy.count() >= 1);
    QCOMPARE(filesSpy.count(), 1);
    QCOMPARE(filesSpy.at(0).at(0).toString(), QStringLiteral("nb-test"));
    QVERIFY(filesSpy.at(0).at(1).toStringList().isEmpty());
  }
  vxcore_context_destroy(ctx);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestEventBridgeSync)
#include "test_eventbridge_sync.moc"
