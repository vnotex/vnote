#include <QtTest>

#include <core/exampleplugin.h>
#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>

namespace tests {

// Integration tests for the hook system with ExamplePlugin.
// Verifies that hooks are properly registered, executed, and can be unregistered.
class TestHookIntegration : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // ExamplePlugin registration tests
  void testExamplePluginRegistersHooks();
  void testExamplePluginUnregistersHooks();

  // Hook execution tests
  void testNotebookOpenHookFires();
  void testNodeActivateHookFires();
  void testMainWindowShowHookFires();
  void testNodeDisplayNameFilterWorks();

  // Cancellation tests
  void testActionCancellationPropagates();

  // Multiple handlers test
  void testMultipleHandlersExecuteInOrder();

private:
  vnotex::HookManager *m_hookMgr = nullptr;
};

void TestHookIntegration::initTestCase() {
  // Nothing to do - tests create fresh HookManager instances
}

void TestHookIntegration::cleanupTestCase() {
  // Nothing to do
}

void TestHookIntegration::init() {
  m_hookMgr = new vnotex::HookManager(this);
}

void TestHookIntegration::cleanup() {
  delete m_hookMgr;
  m_hookMgr = nullptr;
}

void TestHookIntegration::testExamplePluginRegistersHooks() {
  // Before registration, no hooks should exist
  QVERIFY(!m_hookMgr->hasCallbacks(vnotex::HookNames::NotebookBeforeOpen));
  QVERIFY(!m_hookMgr->hasCallbacks(vnotex::HookNames::NodeBeforeActivate));
  QVERIFY(!m_hookMgr->hasCallbacks(vnotex::HookNames::MainWindowAfterShow));
  QVERIFY(!m_hookMgr->hasCallbacks(vnotex::HookNames::FilterNodeDisplayName));

  // Register example plugin hooks
  vnotex::ExamplePlugin::registerHooks(m_hookMgr);

  // After registration, hooks should exist
  QVERIFY(m_hookMgr->hasCallbacks(vnotex::HookNames::NotebookBeforeOpen));
  QVERIFY(m_hookMgr->hasCallbacks(vnotex::HookNames::NodeBeforeActivate));
  QVERIFY(m_hookMgr->hasCallbacks(vnotex::HookNames::MainWindowAfterShow));
  QVERIFY(m_hookMgr->hasCallbacks(vnotex::HookNames::FilterNodeDisplayName));

  // Verify correct callback counts
  QCOMPARE(m_hookMgr->actionCount(vnotex::HookNames::NotebookBeforeOpen), 1);
  QCOMPARE(m_hookMgr->actionCount(vnotex::HookNames::NodeBeforeActivate), 1);
  QCOMPARE(m_hookMgr->actionCount(vnotex::HookNames::MainWindowAfterShow), 1);
  QCOMPARE(m_hookMgr->filterCount(vnotex::HookNames::FilterNodeDisplayName), 1);

  // Clean up
  vnotex::ExamplePlugin::unregisterHooks(m_hookMgr);
}

void TestHookIntegration::testExamplePluginUnregistersHooks() {
  // Register then unregister
  vnotex::ExamplePlugin::registerHooks(m_hookMgr);
  vnotex::ExamplePlugin::unregisterHooks(m_hookMgr);

  // After unregistration, hooks should be gone
  QVERIFY(!m_hookMgr->hasCallbacks(vnotex::HookNames::NotebookBeforeOpen));
  QVERIFY(!m_hookMgr->hasCallbacks(vnotex::HookNames::NodeBeforeActivate));
  QVERIFY(!m_hookMgr->hasCallbacks(vnotex::HookNames::MainWindowAfterShow));
  QVERIFY(!m_hookMgr->hasCallbacks(vnotex::HookNames::FilterNodeDisplayName));
}

void TestHookIntegration::testNotebookOpenHookFires() {
  bool hookFired = false;
  QString receivedNotebookId;

  // Register our own handler to track execution
  m_hookMgr->addAction(
      vnotex::HookNames::NotebookBeforeOpen,
      [&](vnotex::HookContext &p_ctx, const QVariantMap &p_args) {
        Q_UNUSED(p_ctx);
        hookFired = true;
        receivedNotebookId = p_args.value(QStringLiteral("notebookId")).toString();
      },
      10);

  // Fire the hook
  QVariantMap args;
  args[QStringLiteral("notebookId")] = QStringLiteral("test-notebook-123");
  m_hookMgr->doAction(vnotex::HookNames::NotebookBeforeOpen, args);

  // Verify execution
  QVERIFY(hookFired);
  QCOMPARE(receivedNotebookId, QStringLiteral("test-notebook-123"));
}

void TestHookIntegration::testNodeActivateHookFires() {
  bool hookFired = false;
  QString receivedPath;

  m_hookMgr->addAction(
      vnotex::HookNames::NodeBeforeActivate,
      [&](vnotex::HookContext &p_ctx, const QVariantMap &p_args) {
        Q_UNUSED(p_ctx);
        hookFired = true;
        receivedPath = p_args.value(QStringLiteral("relativePath")).toString();
      },
      10);

  QVariantMap args;
  args[QStringLiteral("notebookId")] = QStringLiteral("nb-1");
  args[QStringLiteral("relativePath")] = QStringLiteral("docs/readme.md");
  m_hookMgr->doAction(vnotex::HookNames::NodeBeforeActivate, args);

  QVERIFY(hookFired);
  QCOMPARE(receivedPath, QStringLiteral("docs/readme.md"));
}

void TestHookIntegration::testMainWindowShowHookFires() {
  bool beforeFired = false;
  bool afterFired = false;

  m_hookMgr->addAction(
      vnotex::HookNames::MainWindowBeforeShow,
      [&](vnotex::HookContext &p_ctx, const QVariantMap &p_args) {
        Q_UNUSED(p_ctx);
        Q_UNUSED(p_args);
        beforeFired = true;
      },
      10);

  m_hookMgr->addAction(
      vnotex::HookNames::MainWindowAfterShow,
      [&](vnotex::HookContext &p_ctx, const QVariantMap &p_args) {
        Q_UNUSED(p_ctx);
        Q_UNUSED(p_args);
        afterFired = true;
      },
      10);

  m_hookMgr->doAction(vnotex::HookNames::MainWindowBeforeShow, {});
  QVERIFY(beforeFired);
  QVERIFY(!afterFired);

  m_hookMgr->doAction(vnotex::HookNames::MainWindowAfterShow, {});
  QVERIFY(afterFired);
}

void TestHookIntegration::testNodeDisplayNameFilterWorks() {
  // Register a filter that adds prefix
  m_hookMgr->addFilter(
      vnotex::HookNames::FilterNodeDisplayName,
      [](const QVariant &p_value, const QVariantMap &p_context) -> QVariant {
        Q_UNUSED(p_context);
        return QStringLiteral("[TEST] ") + p_value.toString();
      },
      10);

  // Apply filter
  QVariant result =
      m_hookMgr->applyFilters(vnotex::HookNames::FilterNodeDisplayName, QStringLiteral("MyNote.md"));

  QCOMPARE(result.toString(), QStringLiteral("[TEST] MyNote.md"));
}

void TestHookIntegration::testActionCancellationPropagates() {
  bool firstHandlerCalled = false;
  bool secondHandlerCalled = false;

  // First handler cancels the action
  m_hookMgr->addAction(
      vnotex::HookNames::NotebookBeforeOpen,
      [&](vnotex::HookContext &p_ctx, const QVariantMap &p_args) {
        Q_UNUSED(p_args);
        firstHandlerCalled = true;
        p_ctx.cancel();
      },
      5); // Lower priority = runs first

  // Second handler should still run but cancellation should be returned
  m_hookMgr->addAction(
      vnotex::HookNames::NotebookBeforeOpen,
      [&](vnotex::HookContext &p_ctx, const QVariantMap &p_args) {
        Q_UNUSED(p_ctx);
        Q_UNUSED(p_args);
        secondHandlerCalled = true;
      },
      10);

  bool cancelled = m_hookMgr->doAction(vnotex::HookNames::NotebookBeforeOpen, {});

  QVERIFY(firstHandlerCalled);
  QVERIFY(secondHandlerCalled); // Second handler still runs
  QVERIFY(cancelled);           // But cancellation is returned
}

void TestHookIntegration::testMultipleHandlersExecuteInOrder() {
  QStringList executionOrder;

  m_hookMgr->addAction(
      vnotex::HookNames::NodeBeforeCreate,
      [&](vnotex::HookContext &p_ctx, const QVariantMap &p_args) {
        Q_UNUSED(p_ctx);
        Q_UNUSED(p_args);
        executionOrder << QStringLiteral("priority20");
      },
      20);

  m_hookMgr->addAction(
      vnotex::HookNames::NodeBeforeCreate,
      [&](vnotex::HookContext &p_ctx, const QVariantMap &p_args) {
        Q_UNUSED(p_ctx);
        Q_UNUSED(p_args);
        executionOrder << QStringLiteral("priority5");
      },
      5);

  m_hookMgr->addAction(
      vnotex::HookNames::NodeBeforeCreate,
      [&](vnotex::HookContext &p_ctx, const QVariantMap &p_args) {
        Q_UNUSED(p_ctx);
        Q_UNUSED(p_args);
        executionOrder << QStringLiteral("priority10");
      },
      10);

  m_hookMgr->doAction(vnotex::HookNames::NodeBeforeCreate, {});

  // Should execute in priority order: 5, 10, 20
  QCOMPARE(executionOrder.size(), 3);
  QCOMPARE(executionOrder[0], QStringLiteral("priority5"));
  QCOMPARE(executionOrder[1], QStringLiteral("priority10"));
  QCOMPARE(executionOrder[2], QStringLiteral("priority20"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestHookIntegration)
#include "test_hookintegration.moc"
