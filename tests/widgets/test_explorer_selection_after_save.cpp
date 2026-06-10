// Regression tests for the explorer selection / fs-suppression helpers
// introduced by the explorer-selection-clear-on-mode-switch fix.
//
// Two layers under test:
//
//   1. The two free helpers in src/utils/notebookpathhelpers.h:
//        vnotex::resolveExpectFsChangePathForBuffer
//        vnotex::isPathUnderNotebookRoot
//      These power the FileBeforeSave / sync-started suppression path in
//      NotebookExplorer2. They live as FREE FUNCTIONS specifically so the
//      tests below can exercise them without linking notebookexplorer2.cpp
//      and without instantiating NotebookExplorer2 (the latter is blocked
//      in tests today, per tests/widgets/test_notebookexplorer2_multiselect.cpp:37).
//
//   2. NotebookNodeView::selectNode hardening (walk-ancestors-and-fetchMore
//      + qWarning on unresolvable nodes). We can't reach NotebookNodeView
//      directly (m_view is private inside CombinedNodeExplorer), but
//      CombinedNodeExplorer::selectNode / currentNodeId are PUBLIC overrides
//      of INodeExplorer (see src/views/combinednodeexplorer.h:33, 36) that
//      delegate straight to the view, so they are the canonical surface for
//      this behavior.
//
// Harness mirrors tests/widgets/test_node_explorer_reload_expansion.cpp,
// the only existing test that successfully instantiates a node-explorer
// family widget against a real ServiceLocator.

#include <QtTest>

#include <QApplication>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>

#include <core/configmgr2.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <utils/notebookpathhelpers.h>
#include <views/combinednodeexplorer.h>

#include <vxcore/vxcore.h>

#include "../helpers/temp_dir_fixture.h"

using namespace vnotex;

namespace tests {

class TestExplorerSelectionAfterSave : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  // Helpers from src/utils/notebookpathhelpers.h (Patch A logic).
  void testResolveExpectFsChangePath_RootFile();              // C1
  void testResolveExpectFsChangePath_NestedFile();            // C2
  void testResolveExpectFsChangePath_CrossNotebookFiltered(); // C3a
  void testResolveExpectFsChangePath_InvalidBufferId();       // C3b
  void testIsPathUnderNotebookRoot_True();                    // C3c
  void testIsPathUnderNotebookRoot_DifferentNotebook();       // C3d
  void testIsPathUnderNotebookRoot_PrefixCollision();         // C3e

  // NotebookNodeView::selectNode hardening (Patch B logic) via
  // CombinedNodeExplorer's public INodeExplorer override.
  void testSelectNodeWalksAncestorsAndFetches(); // C4
  void testSelectNodeWarnsOnUnresolvable();      // C4b

private:
  // Build a NodeIdentifier for the seeded notebook at @p_relPath.
  NodeIdentifier idFor(const QString &p_relPath) const;

  // Seed the per-test notebook with a.md at root and sub/b.md nested.
  void seedNotebook();

  // Create a SECOND notebook in the same temp dir (used only by
  // testIsPathUnderNotebookRoot_DifferentNotebook). Returns the new
  // notebook id.
  QString seedSecondNotebook();

  VxCoreContextHandle m_ctx = nullptr;
  ConfigCoreService *m_configCore = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
  NotebookCoreService *m_notebookSvc = nullptr;
  HookManager *m_hookMgr = nullptr;
  BufferService *m_bufferSvc = nullptr;
  ServiceLocator *m_services = nullptr;
  TempDirFixture *m_tempDir = nullptr;
  QString m_nbId;
};

void TestExplorerSelectionAfterSave::initTestCase() {
  // CRITICAL: redirect vxcore to test-mode temp dirs BEFORE any context_create.
  // (Same rule as tests/AGENTS.md and the reference reload-expansion test.)
  vxcore_set_test_mode(1);
}

void TestExplorerSelectionAfterSave::cleanupTestCase() {
  // All per-test resources are torn down in cleanup(); nothing class-wide here.
}

void TestExplorerSelectionAfterSave::init() {
  m_tempDir = new TempDirFixture();
  QVERIFY(m_tempDir->isValid());

  // Fresh vxcore context per test so state never leaks between scenarios.
  const QString ctxConfig = QStringLiteral("{}");
  VxCoreError err = vxcore_context_create(ctxConfig.toUtf8().constData(), &m_ctx);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_ctx != nullptr);

  m_configCore = new ConfigCoreService(m_ctx);
  m_configMgr = new ConfigMgr2(m_configCore);
  // ConfigMgr2::init() loads MainConfig/SessionConfig (or falls back to
  // defaults when no on-disk config exists). Required for getWidgetConfig()
  // inside CombinedNodeExplorer::setupUI(); skipping it crashes that path.
  m_configMgr->init();

  m_notebookSvc = new NotebookCoreService(m_ctx);

  // BufferService needs a HookManager (the fire site for vnote.file.* hooks).
  // No hooks are registered in these tests, but the dependency must exist.
  m_hookMgr = new HookManager();

  // Use the convenience BufferService ctor (bufferservice.h:55-56) — it owns
  // an internal NotebookIoGate so the test does not have to wire one. The
  // production ctor at lines 48-51 takes a shared gate; we don't need that
  // here because nothing in this test races with sync ops.
  m_bufferSvc = new BufferService(m_ctx, m_hookMgr, AutoSavePolicy::None);

  m_services = new ServiceLocator();
  m_services->registerService<ConfigCoreService>(m_configCore);
  m_services->registerService<ConfigMgr2>(m_configMgr);
  m_services->registerService<NotebookCoreService>(m_notebookSvc);
  m_services->registerService<HookManager>(m_hookMgr);
  m_services->registerService<BufferService>(m_bufferSvc);
  // ThemeService / SyncService / KeychainGuard are intentionally NOT
  // registered: the helpers under test do not consume them, and
  // CombinedNodeExplorer accepts a nullptr ThemeService (no show() is
  // called, so paint-time deref never happens). See decisions.md.

  seedNotebook();
}

void TestExplorerSelectionAfterSave::cleanup() {
  // Destroy in reverse construction order — BufferService depends on
  // HookManager + the vxcore context, ConfigMgr2 depends on ConfigCoreService,
  // and everything depends on the vxcore context.
  delete m_services;
  m_services = nullptr;

  delete m_bufferSvc;
  m_bufferSvc = nullptr;

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

  delete m_tempDir;
  m_tempDir = nullptr;

  m_nbId.clear();
}

NodeIdentifier TestExplorerSelectionAfterSave::idFor(const QString &p_relPath) const {
  NodeIdentifier id;
  id.notebookId = m_nbId;
  id.relativePath = p_relPath;
  return id;
}

void TestExplorerSelectionAfterSave::seedNotebook() {
  const QString nbPath = m_tempDir->filePath("nb");
  // NotebookCoreService::createNotebook rejects empty/invalid config JSON.
  const QString cfg = QStringLiteral(R"({"name":"selection_test","version":"1"})");
  m_nbId = m_notebookSvc->createNotebook(nbPath, cfg, NotebookType::Bundled);
  QVERIFY(!m_nbId.isEmpty());

  // a.md at root (used by C1, C3a).
  QVERIFY(!m_notebookSvc->createFile(m_nbId, QString(), QStringLiteral("a.md")).isEmpty());

  // sub/b.md (used by C2, C4).
  QVERIFY(!m_notebookSvc->createFolder(m_nbId, QString(), QStringLiteral("sub")).isEmpty());
  QVERIFY(
      !m_notebookSvc->createFile(m_nbId, QStringLiteral("sub"), QStringLiteral("b.md")).isEmpty());
}

QString TestExplorerSelectionAfterSave::seedSecondNotebook() {
  // NOTE: helpers with a non-void return MUST NOT call QVERIFY*. The QVERIFY*
  // macros expand to a bare `return;` on failure, which MSVC flags as C2561
  // ("function must return a value"). The codebase convention (e.g.,
  // tests/core/test_notebookservice.cpp:119, test_mark_metadata.cpp:77) is to
  // simply return the value and let the CALLER QVERIFY the result. The C3d
  // test below already does `QVERIFY(!otherNbId.isEmpty())` immediately after
  // this call, so the safety net is preserved.
  const QString nbPath = m_tempDir->filePath("nb_other");
  const QString cfg = QStringLiteral(R"({"name":"selection_test_other","version":"1"})");
  return m_notebookSvc->createNotebook(nbPath, cfg, NotebookType::Bundled);
}

// ============================================================================
// Patch A: resolveExpectFsChangePathForBuffer
// ============================================================================

// C1: root-level file (a.md) → expectFsChange path is the notebook root.
void TestExplorerSelectionAfterSave::testResolveExpectFsChangePath_RootFile() {
  Buffer2 buf = m_bufferSvc->openBuffer(idFor(QStringLiteral("a.md")));
  QVERIFY2(buf.isValid(), "failed to open a.md as buffer");

  const QString got = vnotex::resolveExpectFsChangePathForBuffer(*m_services, buf.id(), m_nbId);

  // For a file at the notebook root, QFileInfo("a.md").path() returns ".",
  // which the helper normalizes to empty. buildAbsolutePath(nb, "") is the
  // notebook root directory.
  const QString expected = m_notebookSvc->buildAbsolutePath(m_nbId, QString());
  QCOMPARE(got, expected);
  QVERIFY2(!got.isEmpty(), "notebook root path must not be empty");
}

// C2: nested file (sub/b.md) → expectFsChange path is the parent folder.
void TestExplorerSelectionAfterSave::testResolveExpectFsChangePath_NestedFile() {
  Buffer2 buf = m_bufferSvc->openBuffer(idFor(QStringLiteral("sub/b.md")));
  QVERIFY2(buf.isValid(), "failed to open sub/b.md as buffer");

  const QString got = vnotex::resolveExpectFsChangePathForBuffer(*m_services, buf.id(), m_nbId);

  const QString expected = m_notebookSvc->buildAbsolutePath(m_nbId, QStringLiteral("sub"));
  QCOMPARE(got, expected);
  QVERIFY2(!got.isEmpty(), "nested parent folder path must not be empty");
}

// C3a: buffer belongs to a different notebook than the current one → empty.
//      Prevents NotebookExplorer2 from suppressing fs-watcher events on a
//      notebook that is not currently displayed.
void TestExplorerSelectionAfterSave::testResolveExpectFsChangePath_CrossNotebookFiltered() {
  Buffer2 buf = m_bufferSvc->openBuffer(idFor(QStringLiteral("a.md")));
  QVERIFY2(buf.isValid(), "failed to open a.md as buffer");

  const QString got = vnotex::resolveExpectFsChangePathForBuffer(
      *m_services, buf.id(), QStringLiteral("11111111-2222-3333-4444-555555555555"));

  QVERIFY2(got.isEmpty(), "helper must reject buffers belonging to a different notebook id");
}

// C3b: invalid bufferId → empty (graceful failure).
void TestExplorerSelectionAfterSave::testResolveExpectFsChangePath_InvalidBufferId() {
  // BufferService::getBufferHandle logs a qWarning when the buffer is not
  // found; whitelist it so the assertion below is the only signal.
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression("BufferService::getBufferHandle: buffer not found.*"));

  const QString got = vnotex::resolveExpectFsChangePathForBuffer(
      *m_services, QStringLiteral("garbage-buffer-id"), m_nbId);

  QVERIFY2(got.isEmpty(), "helper must return empty path for invalid bufferId");
}

// ============================================================================
// Patch A: isPathUnderNotebookRoot
// ============================================================================

// C3c: a real subfolder under the notebook root → true.
void TestExplorerSelectionAfterSave::testIsPathUnderNotebookRoot_True() {
  const QString subAbs = m_notebookSvc->buildAbsolutePath(m_nbId, QStringLiteral("sub"));
  QVERIFY(!subAbs.isEmpty());

  QVERIFY2(vnotex::isPathUnderNotebookRoot(*m_services, m_nbId, subAbs),
           "sub/ must be reported as being under the notebook root");

  // Bonus sanity: the root itself is also "under" the root (== match).
  const QString rootAbs = m_notebookSvc->buildAbsolutePath(m_nbId, QString());
  QVERIFY2(vnotex::isPathUnderNotebookRoot(*m_services, m_nbId, rootAbs),
           "notebook root must compare equal to itself under the helper");
}

// C3d: path lives inside a DIFFERENT notebook → false.
//      Guards against cross-notebook suppression.
void TestExplorerSelectionAfterSave::testIsPathUnderNotebookRoot_DifferentNotebook() {
  const QString otherNbId = seedSecondNotebook();
  QVERIFY(!otherNbId.isEmpty());
  QVERIFY(otherNbId != m_nbId);

  const QString otherRootAbs = m_notebookSvc->buildAbsolutePath(otherNbId, QString());
  QVERIFY(!otherRootAbs.isEmpty());

  QVERIFY2(!vnotex::isPathUnderNotebookRoot(*m_services, m_nbId, otherRootAbs),
           "the second notebook's root must NOT be reported as under the first notebook");
}

// C3e: sibling directory whose name shares a prefix with the notebook root
//      must NOT match. Without the trailing-separator guard this would be a
//      false positive (".../nb" startsWith ".../nb" succeeds for ".../nbXXX").
void TestExplorerSelectionAfterSave::testIsPathUnderNotebookRoot_PrefixCollision() {
  // The notebook root is `<tmp>/nb` (see seedNotebook). Construct a sibling
  // that *starts with* the same string but is NOT under it.
  const QString siblingPath = m_tempDir->filePath(QStringLiteral("nb2/anything"));

  QVERIFY2(!vnotex::isPathUnderNotebookRoot(*m_services, m_nbId, siblingPath),
           "sibling directory sharing a name prefix must not be treated as under root");
}

// ============================================================================
// Patch B: NotebookNodeView::selectNode hardening (via CombinedNodeExplorer)
// ============================================================================

// C4: selectNode walks ancestors and fetches their children when the target
//     is a not-yet-fetched descendant. Without the hardening, selectNode
//     would do nothing for sub/b.md when sub/ has never been expanded.
void TestExplorerSelectionAfterSave::testSelectNodeWalksAncestorsAndFetches() {
  CombinedNodeExplorer explorer(*m_services);
  explorer.setNotebookId(m_nbId);

  // Sanity: no folders pre-expanded → sub/'s children cache is empty.
  // (If applyState({"sub"}) were called here the test would be vacuous.)
  QVERIFY2(explorer.captureState().expandedFolders.isEmpty(),
           "test precondition: no folders should be expanded before selectNode");

  // Direct call to the public INodeExplorer override.
  explorer.selectNode(idFor(QStringLiteral("sub/b.md")));

  const NodeIdentifier expected = idFor(QStringLiteral("sub/b.md"));
  QCOMPARE(explorer.currentNodeId(), expected);
}

// C4b: selectNode on an unresolvable path must qWarning + early-return
//      WITHOUT clobbering the previous selection or crashing.
void TestExplorerSelectionAfterSave::testSelectNodeWarnsOnUnresolvable() {
  CombinedNodeExplorer explorer(*m_services);
  explorer.setNotebookId(m_nbId);

  // Establish a known-good current selection first.
  explorer.selectNode(idFor(QStringLiteral("a.md")));
  QCOMPARE(explorer.currentNodeId(), idFor(QStringLiteral("a.md")));

  // The qWarning format (notebooknodeview.cpp:180-181) is:
  //   qWarning() << "NotebookNodeView::selectNode: failed to resolve"
  //              << p_nodeId.notebookId << p_nodeId.relativePath;
  // Match anything mentioning selectNode/failed-to-resolve/nonexistent.
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression("selectNode.*failed to resolve.*nonexistent"));

  // The walk will visit "nonexistent" as the parent, fail to find an index,
  // bail out of the walk, retry once, and qWarning + return.
  explorer.selectNode(idFor(QStringLiteral("nonexistent/file.md")));

  // Selection must be unchanged.
  QCOMPARE(explorer.currentNodeId(), idFor(QStringLiteral("a.md")));
}

} // namespace tests

QTEST_MAIN(tests::TestExplorerSelectionAfterSave)
#include "test_explorer_selection_after_save.moc"
