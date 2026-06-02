// Regression test for the startup AV in EventBridge teardown.
// See .sisyphus/evidence/crash-triage-report.md for the cdb stack proving
// root cause. The fix adds EventBridge::shutdown() as an idempotent unregister
// path called from QCoreApplication::aboutToQuit BEFORE vxcore teardown, so
// ~EventBridge -> vxcore_off_event is a no-op on torn-down EventManager state.
#include <QtTest>

#include <vxcore/vxcore.h>

#include <core/services/eventbridge.h>

namespace tests {

class TestEventBridgeShutdown : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void testIdempotentShutdown();
  void testDtorAfterShutdownIsSafe();

private:
  VxCoreContextHandle m_ctx = nullptr;
};

void TestEventBridgeShutdown::initTestCase() {
  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_ctx);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_ctx != nullptr);
}

void TestEventBridgeShutdown::cleanupTestCase() {
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

void TestEventBridgeShutdown::testIdempotentShutdown() {
  vnotex::EventBridge bridge(m_ctx);
  bridge.shutdown();
  bridge.shutdown(); // second call must not crash and must be a no-op
}

void TestEventBridgeShutdown::testDtorAfterShutdownIsSafe() {
  {
    vnotex::EventBridge bridge(m_ctx);
    bridge.shutdown();
  } // dtor runs shutdown() again — must not crash
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestEventBridgeShutdown)
#include "test_eventbridge_shutdown.moc"
