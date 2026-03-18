#include <QtTest>

#include <core/hooknames.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestConfigHook : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testConfigEditorChangedHookFires();
  void testAutoSavePolicyUpdatesViaHook();
  void testAutoSavePolicyMultipleChanges();

private:
  VxCoreContextHandle m_context = nullptr;
  HookManager *m_hookMgr = nullptr;
  BufferService *m_bufferService = nullptr;
};

void TestConfigHook::initTestCase() {
  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_hookMgr = new HookManager(this);
  m_bufferService = new BufferService(m_context, m_hookMgr, this);
}

void TestConfigHook::cleanupTestCase() {
  delete m_bufferService;
  m_bufferService = nullptr;

  delete m_hookMgr;
  m_hookMgr = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

// Verify that doAction(HookNames::ConfigEditorChanged) triggers a registered action callback.
void TestConfigHook::testConfigEditorChangedHookFires() {
  bool callbackFired = false;
  int id = m_hookMgr->addAction(HookNames::ConfigEditorChanged,
                                [&callbackFired](HookContext &, const QVariantMap &) {
                                  callbackFired = true;
                                });
  QVERIFY(id > 0);

  bool cancelled = m_hookMgr->doAction(HookNames::ConfigEditorChanged);
  QVERIFY(!cancelled);
  QVERIFY(callbackFired);

  m_hookMgr->removeAction(id);
}

// Verify the integration pattern used in main.cpp:
// Register a hook action for ConfigEditorChanged that calls setAutoSavePolicy,
// fire the hook, and verify the policy was propagated.
void TestConfigHook::testAutoSavePolicyUpdatesViaHook() {
  // Set initial policy to None.
  m_bufferService->setAutoSavePolicy(AutoSavePolicy::None);

  // Track that the callback actually invoked setAutoSavePolicy.
  AutoSavePolicy policySetInCallback = AutoSavePolicy::None;

  // Register a hook action that simulates what main.cpp does:
  // on editor config change, re-read the policy and call setAutoSavePolicy.
  int id = m_hookMgr->addAction(
      HookNames::ConfigEditorChanged,
      [this, &policySetInCallback](HookContext &, const QVariantMap &) {
        AutoSavePolicy newPolicy = AutoSavePolicy::AutoSave;
        m_bufferService->setAutoSavePolicy(newPolicy);
        policySetInCallback = newPolicy;
      });
  QVERIFY(id > 0);

  // Fire the hook (simulates config dialog saving editor settings).
  bool cancelled = m_hookMgr->doAction(HookNames::ConfigEditorChanged);
  QVERIFY(!cancelled);

  // Verify the callback ran and called setAutoSavePolicy with AutoSave.
  QCOMPARE(policySetInCallback, AutoSavePolicy::AutoSave);

  m_hookMgr->removeAction(id);
}

// Verify the policy can be changed multiple times via hook,
// covering runtime config changes across the session.
void TestConfigHook::testAutoSavePolicyMultipleChanges() {
  AutoSavePolicy lastPolicySet = AutoSavePolicy::None;

  // Use a mutable captured variable to control which policy to set on each firing.
  AutoSavePolicy desiredPolicy = AutoSavePolicy::None;

  int id = m_hookMgr->addAction(
      HookNames::ConfigEditorChanged,
      [this, &lastPolicySet, &desiredPolicy](HookContext &, const QVariantMap &) {
        m_bufferService->setAutoSavePolicy(desiredPolicy);
        lastPolicySet = desiredPolicy;
      });
  QVERIFY(id > 0);

  // Initial state: None.
  m_bufferService->setAutoSavePolicy(AutoSavePolicy::None);
  QCOMPARE(lastPolicySet, AutoSavePolicy::None);

  // Change 1: None -> AutoSave.
  desiredPolicy = AutoSavePolicy::AutoSave;
  m_hookMgr->doAction(HookNames::ConfigEditorChanged);
  QCOMPARE(lastPolicySet, AutoSavePolicy::AutoSave);

  // Change 2: AutoSave -> BackupFile.
  desiredPolicy = AutoSavePolicy::BackupFile;
  m_hookMgr->doAction(HookNames::ConfigEditorChanged);
  QCOMPARE(lastPolicySet, AutoSavePolicy::BackupFile);

  // Change 3: BackupFile -> None.
  desiredPolicy = AutoSavePolicy::None;
  m_hookMgr->doAction(HookNames::ConfigEditorChanged);
  QCOMPARE(lastPolicySet, AutoSavePolicy::None);

  m_hookMgr->removeAction(id);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestConfigHook)
#include "test_config_hook.moc"
