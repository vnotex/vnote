// Regression tests for explorer-level reloadNode expansion preservation.
//
// Validates the T1 (CombinedNodeExplorer) and T2 (TwoColumnsNodeExplorer)
// patches that capture the current tree expansion before calling the model's
// reloadNode and replay the captured expansion afterward — so the tree shape
// survives the model's begin/endRemoveRows wipe.
//
// All assertions read expansion state via the public INodeExplorer API
// (captureState().expandedFolders / applyState({...})) — no friend access,
// no production-source modification.

#include <QtTest>

#include <QApplication>
#include <QList>
#include <QString>

#include <core/configmgr2.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/configcoreservice.h>
#include <core/services/notebookcoreservice.h>
#include <views/combinednodeexplorer.h>
#include <views/inodeexplorer.h>
#include <views/twocolumnsnodeexplorer.h>

#include <vxcore/vxcore.h>

#include "../helpers/temp_dir_fixture.h"

using namespace vnotex;

namespace tests {

class TestNodeExplorerReloadExpansion : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void init();
  void cleanup();

  void testCombinedSiblingExpansionPreserved();              // (a)
  void testCombinedDescendantExpansionPreserved();           // (b)
  void testCombinedZeroExpandedNoop();                       // (c)
  void testNewNoteAncestorExpandedAndSelected();             // (d)
  void testTwoColumnsFolderReloadPreservesExpansion();       // (e)
  void testTwoColumnsFileReloadLeavesFolderExpansionAlone(); // (f)
  void testExpansionSurvivesRowSortOrderChange();            // (g)

private:
  // Build a NodeIdentifier for the current test notebook at @p_relPath.
  NodeIdentifier idFor(const QString &p_relPath) const;
  NodeIdentifier rootId() const { return idFor(QString()); }

  // Seed the current test notebook with the canonical fixture layout:
  //   A/, A/Achild/, A/Achild/note0.md, B/, B/note1.md, C/, C/note2.md
  void seedFixtureNotebook();

  VxCoreContextHandle m_ctx = nullptr;
  ConfigCoreService *m_configCore = nullptr;
  ConfigMgr2 *m_configMgr = nullptr;
  NotebookCoreService *m_notebookSvc = nullptr;
  ServiceLocator *m_services = nullptr;
  TempDirFixture *m_tempDir = nullptr;
  QString m_nbId;
};

void TestNodeExplorerReloadExpansion::initTestCase() {
  // CRITICAL: redirect vxcore to test-mode temp dirs BEFORE any context_create.
  vxcore_set_test_mode(1);
}

void TestNodeExplorerReloadExpansion::cleanupTestCase() {
  // All per-test resources are torn down in cleanup(); nothing class-wide here.
}

void TestNodeExplorerReloadExpansion::init() {
  m_tempDir = new TempDirFixture();
  QVERIFY(m_tempDir->isValid());

  // Fresh vxcore context per test so state never leaks between scenarios.
  const QString ctxConfig = QStringLiteral("{}");
  VxCoreError err = vxcore_context_create(ctxConfig.toUtf8().constData(), &m_ctx);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_ctx != nullptr);

  m_configCore = new ConfigCoreService(m_ctx);
  m_configMgr = new ConfigMgr2(m_configCore);
  // ConfigMgr2::init() loads MainConfig/SessionConfig (or falls back to defaults
  // when no on-disk config exists). Required so getWidgetConfig() works inside
  // the explorer's setupUI(); skipping it crashes that path.
  m_configMgr->init();

  m_notebookSvc = new NotebookCoreService(m_ctx);

  m_services = new ServiceLocator();
  m_services->registerService<ConfigCoreService>(m_configCore);
  m_services->registerService<ConfigMgr2>(m_configMgr);
  m_services->registerService<NotebookCoreService>(m_notebookSvc);
  // ThemeService is intentionally NOT registered: the delegate paint path is
  // never reached in headless tests (no explorer.show()), and the navigation
  // wrapper accepts a nullptr ThemeService without dereferencing it at
  // construction time.

  seedFixtureNotebook();
}

void TestNodeExplorerReloadExpansion::cleanup() {
  delete m_services;
  m_services = nullptr;

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

NodeIdentifier TestNodeExplorerReloadExpansion::idFor(const QString &p_relPath) const {
  NodeIdentifier id;
  id.notebookId = m_nbId;
  id.relativePath = p_relPath;
  return id;
}

void TestNodeExplorerReloadExpansion::seedFixtureNotebook() {
  const QString nbPath = m_tempDir->filePath("nb");
  // NotebookCoreService::createNotebook rejects empty/invalid config JSON,
  // so we always pass a minimal valid object.
  const QString cfg = QStringLiteral(R"({"name":"reload_test","version":"1"})");
  m_nbId = m_notebookSvc->createNotebook(nbPath, cfg, NotebookType::Bundled);
  QVERIFY(!m_nbId.isEmpty());

  // Canonical fixture: 3 top-level folders, one nested folder under A, and one
  // file per leaf folder. This shape exercises sibling expansion (A/B/C),
  // descendant expansion (A → A/Achild), and sort-order changes (a folder
  // "0_new" prepended in scenario g).
  QVERIFY(!m_notebookSvc->createFolder(m_nbId, QString(), QStringLiteral("A")).isEmpty());
  QVERIFY(!m_notebookSvc->createFolder(m_nbId, QStringLiteral("A"), QStringLiteral("Achild"))
               .isEmpty());
  QVERIFY(!m_notebookSvc->createFile(m_nbId, QStringLiteral("A/Achild"), QStringLiteral("note0.md"))
               .isEmpty());
  QVERIFY(!m_notebookSvc->createFolder(m_nbId, QString(), QStringLiteral("B")).isEmpty());
  QVERIFY(!m_notebookSvc->createFile(m_nbId, QStringLiteral("B"), QStringLiteral("note1.md"))
               .isEmpty());
  QVERIFY(!m_notebookSvc->createFolder(m_nbId, QString(), QStringLiteral("C")).isEmpty());
  QVERIFY(!m_notebookSvc->createFile(m_nbId, QStringLiteral("C"), QStringLiteral("note2.md"))
               .isEmpty());
}

// (a) Combined: sibling folders A and B expanded → reloadNode(root) → both still expanded.
void TestNodeExplorerReloadExpansion::testCombinedSiblingExpansionPreserved() {
  CombinedNodeExplorer explorer(*m_services);
  explorer.setNotebookId(m_nbId);

  NodeExplorerState pre;
  pre.expandedFolders = {idFor(QStringLiteral("A")), idFor(QStringLiteral("B"))};
  explorer.applyState(pre);

  const auto seeded = explorer.captureState().expandedFolders;
  QVERIFY(seeded.contains(idFor(QStringLiteral("A"))));
  QVERIFY(seeded.contains(idFor(QStringLiteral("B"))));

  // Reload the root — without the T1 patch this collapses every top-level folder.
  explorer.reloadNode(rootId());

  const auto post = explorer.captureState().expandedFolders;
  QVERIFY2(post.contains(idFor(QStringLiteral("A"))),
           "A should remain expanded after reloadNode(root)");
  QVERIFY2(post.contains(idFor(QStringLiteral("B"))),
           "B should remain expanded after reloadNode(root)");
}

// (b) Combined: A and A/Achild both expanded → reloadNode(A) → descendant expansion preserved.
void TestNodeExplorerReloadExpansion::testCombinedDescendantExpansionPreserved() {
  CombinedNodeExplorer explorer(*m_services);
  explorer.setNotebookId(m_nbId);

  // Parent must appear before child so replayExpandedFolders can fetchMore
  // ancestors before trying to expand descendants.
  NodeExplorerState pre;
  pre.expandedFolders = {idFor(QStringLiteral("A")), idFor(QStringLiteral("A/Achild"))};
  explorer.applyState(pre);

  const auto seeded = explorer.captureState().expandedFolders;
  QVERIFY(seeded.contains(idFor(QStringLiteral("A"))));
  QVERIFY(seeded.contains(idFor(QStringLiteral("A/Achild"))));

  explorer.reloadNode(idFor(QStringLiteral("A")));

  const auto post = explorer.captureState().expandedFolders;
  QVERIFY2(post.contains(idFor(QStringLiteral("A"))),
           "A should remain expanded after reloadNode(A)");
  QVERIFY2(post.contains(idFor(QStringLiteral("A/Achild"))),
           "A/Achild should remain expanded after reloadNode(A)");
}

// (c) Combined: nothing expanded → reloadNode(root) is a safe no-op (no crash, no spurious expand).
void TestNodeExplorerReloadExpansion::testCombinedZeroExpandedNoop() {
  CombinedNodeExplorer explorer(*m_services);
  explorer.setNotebookId(m_nbId);

  QVERIFY(explorer.captureState().expandedFolders.isEmpty());

  explorer.reloadNode(rootId());

  QVERIFY2(explorer.captureState().expandedFolders.isEmpty(),
           "reloadNode(root) on an unexpanded tree must not spuriously expand anything");
}

// (d) Combined: A & B expanded; insert a new note under C; reloadNode(C) +
//     expandToNode(newNote) → original expansions kept AND C now expanded AND new note selected.
void TestNodeExplorerReloadExpansion::testNewNoteAncestorExpandedAndSelected() {
  CombinedNodeExplorer explorer(*m_services);
  explorer.setNotebookId(m_nbId);

  NodeExplorerState pre;
  pre.expandedFolders = {idFor(QStringLiteral("A")), idFor(QStringLiteral("B"))};
  explorer.applyState(pre);

  // Create the new note before reload so the reload picks up the new row.
  QVERIFY(!m_notebookSvc->createFile(m_nbId, QStringLiteral("C"), QStringLiteral("newNote.md"))
               .isEmpty());
  const NodeIdentifier newNoteId = idFor(QStringLiteral("C/newNote.md"));

  explorer.reloadNode(idFor(QStringLiteral("C")));
  // expandToNode walks ancestors of newNoteId — that's the C folder.
  explorer.expandToNode(newNoteId);
  explorer.selectNode(newNoteId);

  const auto state = explorer.captureState();
  QVERIFY2(state.expandedFolders.contains(idFor(QStringLiteral("A"))),
           "A expansion (set BEFORE reload) must survive reloadNode(C)");
  QVERIFY2(state.expandedFolders.contains(idFor(QStringLiteral("B"))),
           "B expansion (set BEFORE reload) must survive reloadNode(C)");
  QVERIFY2(state.expandedFolders.contains(idFor(QStringLiteral("C"))),
           "C should be expanded after expandToNode(newNoteId)");
  QCOMPARE(state.currentNodeId, newNoteId);
}

// (e) TwoColumns: A & B expanded in the left (folder) panel → reloadNode(root, isFolder=true)
//     → folder-panel expansion preserved by the T2 patch.
void TestNodeExplorerReloadExpansion::testTwoColumnsFolderReloadPreservesExpansion() {
  TwoColumnsNodeExplorer explorer(*m_services);
  explorer.setNotebookId(m_nbId);

  NodeExplorerState pre;
  pre.expandedFolders = {idFor(QStringLiteral("A")), idFor(QStringLiteral("B"))};
  explorer.applyState(pre);

  const auto seeded = explorer.captureState().expandedFolders;
  QVERIFY(seeded.contains(idFor(QStringLiteral("A"))));
  QVERIFY(seeded.contains(idFor(QStringLiteral("B"))));

  // Use the explicit two-arg overload so we exercise the patched code path
  // (single-arg overload would also work — it delegates to the two-arg form).
  explorer.reloadNode(rootId(), /*p_isFolder=*/true);

  const auto post = explorer.captureState().expandedFolders;
  QVERIFY2(post.contains(idFor(QStringLiteral("A"))),
           "A folder expansion should survive TwoColumns reloadNode(root, true)");
  QVERIFY2(post.contains(idFor(QStringLiteral("B"))),
           "B folder expansion should survive TwoColumns reloadNode(root, true)");
}

// (f) TwoColumns: A & B expanded; reloadNode(B/note1.md, isFolder=false) → folder-panel
//     expansion is byte-identical (file-panel reload must NOT touch folder expansion).
void TestNodeExplorerReloadExpansion::testTwoColumnsFileReloadLeavesFolderExpansionAlone() {
  TwoColumnsNodeExplorer explorer(*m_services);
  explorer.setNotebookId(m_nbId);

  NodeExplorerState pre;
  pre.expandedFolders = {idFor(QStringLiteral("A")), idFor(QStringLiteral("B"))};
  pre.displayRootId = idFor(QStringLiteral("B"));
  pre.currentNodeId = idFor(QStringLiteral("B/note1.md"));
  explorer.applyState(pre);

  const auto before = explorer.captureState().expandedFolders;
  QVERIFY(before.contains(idFor(QStringLiteral("A"))));
  QVERIFY(before.contains(idFor(QStringLiteral("B"))));

  // File-panel reload — T2 patch deliberately does NOT capture/replay folder
  // expansion in this branch (no model reload there), so the existing folder
  // expansion is untouched by the call.
  explorer.reloadNode(idFor(QStringLiteral("B/note1.md")), /*p_isFolder=*/false);

  const auto after = explorer.captureState().expandedFolders;
  // Byte-identical comparison: BFS order from NotebookNodeView::getExpandedFolders
  // is stable when the tree shape is unchanged.
  QCOMPARE(after, before);
}

// (g) Combined: A, B, C expanded; insert a new folder "0_new" that sort-orders BEFORE A;
//     reloadNode(root) → expansion is keyed by NodeIdentifier, so a row-order shift
//     does NOT lose any expansion entry.
void TestNodeExplorerReloadExpansion::testExpansionSurvivesRowSortOrderChange() {
  CombinedNodeExplorer explorer(*m_services);
  explorer.setNotebookId(m_nbId);

  NodeExplorerState pre;
  pre.expandedFolders = {idFor(QStringLiteral("A")), idFor(QStringLiteral("B")),
                         idFor(QStringLiteral("C"))};
  explorer.applyState(pre);

  // Insert a new folder that sorts BEFORE A under default alpha order.
  QVERIFY(!m_notebookSvc->createFolder(m_nbId, QString(), QStringLiteral("0_new")).isEmpty());
  QVERIFY(!m_notebookSvc->createFile(m_nbId, QStringLiteral("0_new"), QStringLiteral("z.md"))
               .isEmpty());

  explorer.reloadNode(rootId());

  const auto post = explorer.captureState().expandedFolders;
  QVERIFY2(post.contains(idFor(QStringLiteral("A"))),
           "A expansion must survive insertion of a sibling that re-orders the row layout");
  QVERIFY2(post.contains(idFor(QStringLiteral("B"))),
           "B expansion must survive insertion of a sibling that re-orders the row layout");
  QVERIFY2(post.contains(idFor(QStringLiteral("C"))),
           "C expansion must survive insertion of a sibling that re-orders the row layout");
}

} // namespace tests

QTEST_MAIN(tests::TestNodeExplorerReloadExpansion)
#include "test_node_explorer_reload_expansion.moc"
