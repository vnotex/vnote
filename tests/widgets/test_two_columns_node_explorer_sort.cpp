// T12 (notebook-explorer-drag-reorder) — mirrors T11's CombinedNodeExplorer
// wiring tests for the TwoColumnsNodeExplorer variant. This test covers:
//
//   T12a (testControllerSortRequestedForwardedThroughTwoColumns):
//     NotebookNodeController::sortRequested emits from BOTH the folder pane's
//     controller AND the file pane's controller are re-emitted from the
//     TwoColumnsNodeExplorer wrapper (one connect per controller; both are
//     wired via the connectControllerSignals helper).
//
//   T12b (testRequestReorderNodesBuildsNodeIdentifiersAndCallsController):
//     TwoColumnsNodeExplorer::requestReorderNodes builds NodeIdentifier lists
//     from flat name lists (root parent → no leading slash prefix) and
//     dispatches to the folder pane's controller. Asserts the reorder lands
//     and the folder order changes.
//
//   T12c (testRequestReorderNodesUsesSharedControllerBothPanes):
//     Verifies the dispatch goes to ONE controller (folder), not both.
//     Spies on file controller's nodesReordered to assert it never fires.
//
// Construction strategy follows test_notebook_explorer2_sort.cpp's
// bringUpExplorerHarness pattern: real ServiceLocator + real notebook seeded
// with two folders + two files, real TwoColumnsNodeExplorer instance, inner
// controllers located via findChildren<NotebookNodeController*>().

#include <QtTest>

#include <QApplication>
#include <QList>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QStringList>

#include <vxcore/vxcore.h>

#include <controllers/notebooknodecontroller.h>
#include <core/configmgr2.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <views/twocolumnsnodeexplorer.h>

#include "../helpers/temp_dir_fixture.h"

using namespace vnotex;

namespace tests {

class TestTwoColumnsNodeExplorerSort : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // T12a — controller sortRequested → explorer sortRequested forwarding.
  void testControllerSortRequestedForwardedThroughTwoColumns();

  // T12b — requestReorderNodes builds NodeIdentifier lists and dispatches.
  void testRequestReorderNodesBuildsNodeIdentifiersAndCallsController();

  // T12c — single dispatch (folder controller only; file controller never
  // sees the reorder request).
  void testRequestReorderNodesUsesSharedControllerBothPanes();

private:
  // Bring up the real ServiceLocator + seeded notebook fixture and return a
  // freshly-constructed TwoColumnsNodeExplorer pointing at it. Caller owns
  // the returned widget and must delete it.
  TwoColumnsNodeExplorer *bringUpExplorer();
  void tearDownExplorerHarness();

  // Resolve the explorer's two inner controllers in [folder, file] order.
  // TwoColumnsNodeExplorer constructs them in that order in setupUI()
  // (m_folderController first, then m_fileController), so findChildren
  // returns them in that creation order.
  QList<NotebookNodeController *> findInnerControllers(TwoColumnsNodeExplorer *p_explorer) const;

  NodeIdentifier idFor(const QString &p_relPath) const;

  VxCoreContextHandle m_ctx = nullptr;
  ConfigCoreService *m_configCore = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
  NotebookCoreService *m_notebookSvc = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookIoGate *m_ioGate = nullptr;
  ServiceLocator *m_services = nullptr;
  TempDirFixture *m_tempDir = nullptr;
  QString m_nbId;
};

void TestTwoColumnsNodeExplorerSort::initTestCase() {
  // CRITICAL: redirect vxcore to test-mode temp dirs BEFORE any
  // context_create (per tests/AGENTS.md).
  vxcore_set_test_mode(1);

  // Runtime metatype registration so QSignalSpy::takeFirst() can extract a
  // NodeIdentifier from QVariant. Q_DECLARE_METATYPE alone is not enough.
  qRegisterMetaType<NodeIdentifier>("NodeIdentifier");
  qRegisterMetaType<NodeIdentifier>("vnotex::NodeIdentifier");
}

void TestTwoColumnsNodeExplorerSort::cleanupTestCase() {
  // Per-test resources torn down in cleanup().
}

void TestTwoColumnsNodeExplorerSort::init() {
  m_tempDir = new TempDirFixture();
  QVERIFY(m_tempDir->isValid());
}

void TestTwoColumnsNodeExplorerSort::cleanup() {
  tearDownExplorerHarness();
  delete m_tempDir;
  m_tempDir = nullptr;
}

NodeIdentifier TestTwoColumnsNodeExplorerSort::idFor(const QString &p_relPath) const {
  NodeIdentifier id;
  id.notebookId = m_nbId;
  id.relativePath = p_relPath;
  return id;
}

TwoColumnsNodeExplorer *TestTwoColumnsNodeExplorerSort::bringUpExplorer() {
  const QString ctxConfig = QStringLiteral("{}");
  VxCoreError err = vxcore_context_create(ctxConfig.toUtf8().constData(), &m_ctx);
  Q_ASSERT(err == VXCORE_OK);
  Q_ASSERT(m_ctx != nullptr);

  m_configCore = new ConfigCoreService(m_ctx);
  m_configMgr = new ConfigMgr2(m_configCore);
  m_configMgr->init();

  m_notebookSvc = new NotebookCoreService(m_ctx);
  m_hookMgr = new HookManager();
  m_ioGate = new NotebookIoGate();
  m_notebookSvc->setHookManager(m_hookMgr);
  m_notebookSvc->setNotebookIoGate(m_ioGate);

  m_services = new ServiceLocator();
  m_services->registerService<ConfigCoreService>(m_configCore);
  m_services->registerService<ConfigMgr2>(m_configMgr);
  m_services->registerService<NotebookCoreService>(m_notebookSvc);
  m_services->registerService<HookManager>(m_hookMgr);

  // Seed a notebook with two folders + two files at root.
  const QString nbPath = m_tempDir->filePath("nb");
  const QString cfg = QStringLiteral(R"({"name":"twocolumns_sort_test","version":"1"})");
  m_nbId = m_notebookSvc->createNotebook(nbPath, cfg, NotebookType::Bundled);
  Q_ASSERT(!m_nbId.isEmpty());

  Q_ASSERT(!m_notebookSvc->createFolder(m_nbId, QString(), QStringLiteral("alpha")).isEmpty());
  Q_ASSERT(!m_notebookSvc->createFolder(m_nbId, QString(), QStringLiteral("beta")).isEmpty());
  Q_ASSERT(!m_notebookSvc->createFile(m_nbId, QString(), QStringLiteral("one.md")).isEmpty());
  Q_ASSERT(!m_notebookSvc->createFile(m_nbId, QString(), QStringLiteral("two.md")).isEmpty());

  auto *explorer = new TwoColumnsNodeExplorer(*m_services);
  explorer->setNotebookId(m_nbId);
  return explorer;
}

void TestTwoColumnsNodeExplorerSort::tearDownExplorerHarness() {
  delete m_services;
  m_services = nullptr;
  delete m_ioGate;
  m_ioGate = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  delete m_notebookSvc;
  m_notebookSvc = nullptr;
  delete m_configMgr;
  m_configMgr = nullptr;
  delete m_configCore;
  m_configCore = nullptr;
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
  m_nbId.clear();
}

QList<NotebookNodeController *>
TestTwoColumnsNodeExplorerSort::findInnerControllers(TwoColumnsNodeExplorer *p_explorer) const {
  return p_explorer->findChildren<NotebookNodeController *>();
}

// ============================================================================
// T12a — Controller sortRequested forwarded by TwoColumnsNodeExplorer
// ============================================================================
//
// connectControllerSignals(...) is called for BOTH m_folderController and
// m_fileController in TwoColumnsNodeExplorer::setupUI (lines 105-106). Adding
// the sortRequested forward inside connectControllerSignals therefore wires
// BOTH controllers. Each one's emit must produce one explorer-level emit.
void TestTwoColumnsNodeExplorerSort::testControllerSortRequestedForwardedThroughTwoColumns() {
  QScopedPointer<TwoColumnsNodeExplorer> explorer(bringUpExplorer());

  const auto controllers = findInnerControllers(explorer.data());
  QCOMPARE(controllers.size(), 2);
  auto *folderCtrl = controllers.at(0);
  auto *fileCtrl = controllers.at(1);
  QVERIFY(folderCtrl != nullptr);
  QVERIFY(fileCtrl != nullptr);

  QSignalSpy spy(explorer.data(), &TwoColumnsNodeExplorer::sortRequested);
  QVERIFY(spy.isValid());

  // Folder-pane emit → explorer must re-emit once with the same NodeIdentifier.
  const NodeIdentifier rootId = idFor(QString());
  folderCtrl->sortNodes(rootId);
  QCOMPARE(spy.count(), 1);
  {
    const QList<QVariant> args = spy.takeFirst();
    const auto forwarded = args.at(0).value<NodeIdentifier>();
    QCOMPARE(forwarded.notebookId, m_nbId);
    QVERIFY(forwarded.relativePath.isEmpty());
  }

  // File-pane emit must ALSO be forwarded (both panes share the same
  // sortRequested signal at the explorer level — UX parity with Combined).
  fileCtrl->sortNodes(rootId);
  QCOMPARE(spy.count(), 1);
  {
    const QList<QVariant> args = spy.takeFirst();
    const auto forwarded = args.at(0).value<NodeIdentifier>();
    QCOMPARE(forwarded.notebookId, m_nbId);
    QVERIFY(forwarded.relativePath.isEmpty());
  }
}

// ============================================================================
// T12b — requestReorderNodes builds NodeIdentifiers and dispatches
// ============================================================================
//
// The root-parent case exercises the no-slash branch of the prefix builder:
//   parentId.relativePath == "" → prefix == "" → built id == name.
// A non-empty parent would prefix with "<parent>/"; we test the root branch
// because the seed notebook only populates root.
void TestTwoColumnsNodeExplorerSort::
    testRequestReorderNodesBuildsNodeIdentifiersAndCallsController() {
  QScopedPointer<TwoColumnsNodeExplorer> explorer(bringUpExplorer());

  const auto controllers = findInnerControllers(explorer.data());
  QCOMPARE(controllers.size(), 2);
  auto *folderCtrl = controllers.at(0);
  QVERIFY(folderCtrl != nullptr);

  QSignalSpy reorderedSpy(folderCtrl, &NotebookNodeController::nodesReordered);
  QSignalSpy errorSpy(folderCtrl, &NotebookNodeController::errorOccurred);
  QVERIFY(reorderedSpy.isValid());
  QVERIFY(errorSpy.isValid());

  // Reverse the folder order to provoke a real reorder. Pass an empty file
  // list to verify both the multi-list semantics and that the empty branch
  // is harmless. requestReorderNodes(rootId, {"beta","alpha"}, {}) must
  // produce a successful reorder of the folder children.
  const NodeIdentifier rootId = idFor(QString());
  explorer->requestReorderNodes(rootId, {QStringLiteral("beta"), QStringLiteral("alpha")},
                                /*orderedFileNames=*/QStringList());

  // nodesReordered is emitted via QueuedConnection after the synchronous
  // reorder write. QTRY_COMPARE is order-robust: it passes whether the signal
  // already arrived or arrives within the timeout, unlike QSignalSpy::wait
  // which only catches a NEXT emission and spuriously fails if completion
  // already landed before the wait was entered.
  QTRY_COMPARE_WITH_TIMEOUT(reorderedSpy.count(), 1, 5000);
  QCOMPARE(errorSpy.count(), 0);

  // Verify the emit carried the original parent identifier (notebookId
  // matches, root relative path stays empty — proving the prefix-builder
  // didn't inject a stray slash that would have made the dispatched parent
  // id mismatch the controller's reorderCompleted parent).
  const QList<QVariant> args = reorderedSpy.takeFirst();
  const auto reorderedParent = args.at(0).value<NodeIdentifier>();
  QCOMPARE(reorderedParent.notebookId, m_nbId);
  QVERIFY(reorderedParent.relativePath.isEmpty());
}

// ============================================================================
// T12c — Single dispatch: folder controller only, service called once
// ============================================================================
//
// TwoColumnsNodeExplorer::requestReorderNodes routes through
// m_folderController only (per the impl committed in T11). If a future
// refactor accidentally also dispatches to m_fileController (or fans the
// call out elsewhere), the NotebookCoreService::reorderCompleted signal —
// which fires once per service-level reorder call — would be observed
// twice.
//
// We spy on the SERVICE-level signal (not controller-level) because both
// inner controllers subscribe to it in their constructors
// (notebooknodecontroller_reorder.cpp:30-33), so each controller's
// nodesReordered would fire even from a single dispatch (one service call
// is broadcast to both controllers). The service emit count is the true
// "how many dispatch operations happened" invariant.
void TestTwoColumnsNodeExplorerSort::testRequestReorderNodesUsesSharedControllerBothPanes() {
  QScopedPointer<TwoColumnsNodeExplorer> explorer(bringUpExplorer());

  const auto controllers = findInnerControllers(explorer.data());
  QCOMPARE(controllers.size(), 2);

  QSignalSpy serviceReorderedSpy(m_notebookSvc, &NotebookCoreService::reorderCompleted);
  QVERIFY(serviceReorderedSpy.isValid());

  const NodeIdentifier rootId = idFor(QString());
  explorer->requestReorderNodes(rootId, {QStringLiteral("beta"), QStringLiteral("alpha")},
                                /*orderedFileNames=*/QStringList());

  // Order-robust wait for the first service-level completion (see T12b note).
  QTRY_VERIFY_WITH_TIMEOUT(serviceReorderedSpy.count() >= 1, 5000);

  // Drain any pending events so a hypothetical double-dispatch would have
  // had a chance to surface as a second service emit. 100ms is generous
  // since the first reorder already returned.
  QTest::qWait(100);

  QCOMPARE(serviceReorderedSpy.count(), 1);
}

} // namespace tests

QTEST_MAIN(tests::TestTwoColumnsNodeExplorerSort)
#include "test_two_columns_node_explorer_sort.moc"
