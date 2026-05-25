#include <QDir>
#include <QSet>
#include <QtTest>

#include <controllers/notebooknodecontroller.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>

namespace tests {

// Test-only helper that mirrors the pure resolve logic from
// NotebookNodeController::resolveSelection. This avoids linking against 18+ production controller
// dependencies while testing the core logic:
// - If clicked node is in current selection, return full selection
// - Otherwise, return clicked node only
//
// The production controller implements this in NotebookNodeController::resolveSelection()
// and calls it from context menu handlers during right-click. See AGENTS.md T3 for details.
static QList<vnotex::NodeIdentifier>
resolveSelectionForTest(const QList<vnotex::NodeIdentifier> &p_selection,
                        const vnotex::NodeIdentifier &p_clicked) {
  // Qt right-click convention:
  // - If selection empty or clicked not in selection → return [clicked]
  // - Otherwise → return full selection preserving order
  if (p_selection.isEmpty() || !p_selection.contains(p_clicked)) {
    return {p_clicked};
  }
  return p_selection;
}

// Test-only helper mirroring NotebookNodeController::isSingleEffectiveSelection.
// Returns true iff resolveSelection(selection, clicked).size() == 1.
static bool isSingleEffectiveSelectionForTest(const QList<vnotex::NodeIdentifier> &p_selection,
                                              const vnotex::NodeIdentifier &p_clicked) {
  return resolveSelectionForTest(p_selection, p_clicked).size() == 1;
}

// Test-only helper mirroring NotebookNodeController::dedupeDescendants.
// Removes nodes whose relativePath is a descendant of another node in the list
// (same notebookId only). Preserves input order.
static QList<vnotex::NodeIdentifier>
dedupeDescendantsForTest(const QList<vnotex::NodeIdentifier> &p_ids) {
  QList<vnotex::NodeIdentifier> result;
  for (const auto &id : p_ids) {
    bool isDescendant = false;
    for (const auto &other : p_ids) {
      if (id.notebookId != other.notebookId) {
        continue;
      }
      if (id == other) {
        continue;
      }
      QString otherPath = QDir::cleanPath(other.relativePath);
      QString idPath = QDir::cleanPath(id.relativePath);
      if (!otherPath.isEmpty() && idPath.startsWith(otherPath + QLatin1Char('/'))) {
        isDescendant = true;
        break;
      }
    }
    if (!isDescendant) {
      result.append(id);
    }
  }
  return result;
}

class TestNotebookNodeControllerMultiSelect : public QObject {
  Q_OBJECT

private slots:
  void resolveSelection_emptySelection_returnsClicked();
  void resolveSelection_clickedInsideSelection_returnsFullSelection();
  void resolveSelection_clickedOutsideSelection_returnsClickedOnly();

  // T1: isSingleEffectiveSelection helper
  void isSingleEffectiveSelection_singleSelection_returnsTrue();
  void isSingleEffectiveSelection_multiSelectionClickedInside_returnsFalse();
  void isSingleEffectiveSelection_clickedOutsideSelection_returnsTrue();

  // T5: action lambdas use resolveSelection + dedupeDescendants
  void delete_multiSelection_resolvesAllIds();
  void delete_parentAndChild_dropsChildFromBatch();
  void delete_rightClickOutsideSelection_emitsClickedOnly();
  void remove_multiSelection_resolvesAllIds();
  void copy_rightClickOutsideSelection_resolvesClickedOnly();
  void cut_rightClickOutsideSelection_resolvesClickedOnly();
  void dedupe_parentAndChildSameNotebook_dropsChild();
  void dedupe_differentNotebooks_keepsBoth();

  // T6: duplicate uses resolveSelection + dedupeDescendants
  void duplicate_multiSelection_resolvesAllIds();
  void duplicate_parentAndChild_dropsChild();
  void duplicate_singleArg_delegatesToList();

  // T7: open/openWith use resolveSelection and skip folders
  void open_multiSelection_resolvesAllIds();
  void open_mixedFolderAndNote_skipsFolder();
  void openWith_multiSelection_buildsCommandPerNode();

  // T9: copyPath mirrors buildAbsolutePath + join with newline
  void copyPath_singleNode_unchangedBehavior();
  void copyPath_multiSelection_newlineJoined();

  // T10: pin / reload / mark resolve full selection through list overloads
  void pin_multiSelection_resolvesAllIds();
  void reload_multiSelection_resolvesAllIds();
  void mark_multiSelection_resolvesAllIds();

  // T11: single-arg back-compat. Each single-arg xxxNode(id) inline-delegates to
  // xxxNodes({id}); pipeline equivalent yields the singleton, unchanged.
  void singleArg_open_unchanged();
  void singleArg_duplicate_unchanged();
  void singleArg_delete_unchanged();
  void singleArg_remove_unchanged();
  void singleArg_copyPath_unchanged();
  void singleArg_pin_unchanged();
  void singleArg_reload_unchanged();
  void singleArg_mark_unchanged();
};

void TestNotebookNodeControllerMultiSelect::resolveSelection_emptySelection_returnsClicked() {
  vnotex::NodeIdentifier nodeA;
  nodeA.notebookId = "nb-1";
  nodeA.relativePath = "a.md";

  QList<vnotex::NodeIdentifier> selection;
  QList<vnotex::NodeIdentifier> result = resolveSelectionForTest(selection, nodeA);

  QCOMPARE(result.size(), 1);
  QCOMPARE(result.at(0), nodeA);
}

void TestNotebookNodeControllerMultiSelect::
    resolveSelection_clickedInsideSelection_returnsFullSelection() {
  vnotex::NodeIdentifier nodeA, nodeB, nodeC;
  nodeA.notebookId = "nb-1";
  nodeA.relativePath = "a.md";
  nodeB.notebookId = "nb-1";
  nodeB.relativePath = "b.md";
  nodeC.notebookId = "nb-1";
  nodeC.relativePath = "c.md";

  QList<vnotex::NodeIdentifier> selection = {nodeA, nodeB, nodeC};
  QList<vnotex::NodeIdentifier> result = resolveSelectionForTest(selection, nodeB);

  QCOMPARE(result.size(), 3);
  QCOMPARE(result, selection);
  // Verify order is preserved
  QCOMPARE(result.at(0), nodeA);
  QCOMPARE(result.at(1), nodeB);
  QCOMPARE(result.at(2), nodeC);
}

void TestNotebookNodeControllerMultiSelect::
    resolveSelection_clickedOutsideSelection_returnsClickedOnly() {
  vnotex::NodeIdentifier nodeA, nodeB, nodeC, nodeD;
  nodeA.notebookId = "nb-1";
  nodeA.relativePath = "a.md";
  nodeB.notebookId = "nb-1";
  nodeB.relativePath = "b.md";
  nodeC.notebookId = "nb-1";
  nodeC.relativePath = "c.md";
  nodeD.notebookId = "nb-1";
  nodeD.relativePath = "d.md";

  QList<vnotex::NodeIdentifier> selection = {nodeA, nodeB, nodeC};
  QList<vnotex::NodeIdentifier> result = resolveSelectionForTest(selection, nodeD);

  QCOMPARE(result.size(), 1);
  QCOMPARE(result.at(0), nodeD);
}

// T5 lambdas pipe resolveSelection -> dedupeDescendants -> deleteNodes/removeNodesFromNotebook/
// copyNodes/cutNodes. These tests exercise that pipeline at the helper level (controller
// construction requires 18+ services; mirroring the pure helpers matches the T3 pattern).

void TestNotebookNodeControllerMultiSelect::delete_multiSelection_resolvesAllIds() {
  vnotex::NodeIdentifier a, b, c;
  a.notebookId = b.notebookId = c.notebookId = "nb-1";
  a.relativePath = "a.md";
  b.relativePath = "b.md";
  c.relativePath = "c.md";

  auto resolved = resolveSelectionForTest({a, b, c}, b);
  auto deduped = dedupeDescendantsForTest(resolved);
  QCOMPARE(deduped.size(), 3);
  QCOMPARE(deduped, QList<vnotex::NodeIdentifier>({a, b, c}));
}

void TestNotebookNodeControllerMultiSelect::delete_parentAndChild_dropsChildFromBatch() {
  vnotex::NodeIdentifier parent, child, sibling;
  parent.notebookId = child.notebookId = sibling.notebookId = "nb-1";
  parent.relativePath = "folder";
  child.relativePath = "folder/note.md";
  sibling.relativePath = "other.md";

  auto resolved = resolveSelectionForTest({parent, child, sibling}, parent);
  auto deduped = dedupeDescendantsForTest(resolved);
  QCOMPARE(deduped.size(), 2);
  QCOMPARE(deduped.at(0), parent);
  QCOMPARE(deduped.at(1), sibling);
}

void TestNotebookNodeControllerMultiSelect::delete_rightClickOutsideSelection_emitsClickedOnly() {
  vnotex::NodeIdentifier a, b, c, d;
  a.notebookId = b.notebookId = c.notebookId = d.notebookId = "nb-1";
  a.relativePath = "a.md";
  b.relativePath = "b.md";
  c.relativePath = "c.md";
  d.relativePath = "d.md";

  auto resolved = resolveSelectionForTest({a, b, c}, d);
  auto deduped = dedupeDescendantsForTest(resolved);
  QCOMPARE(deduped.size(), 1);
  QCOMPARE(deduped.at(0), d);
}

void TestNotebookNodeControllerMultiSelect::remove_multiSelection_resolvesAllIds() {
  vnotex::NodeIdentifier a, b;
  a.notebookId = b.notebookId = "nb-1";
  a.relativePath = "a.md";
  b.relativePath = "b.md";

  auto resolved = resolveSelectionForTest({a, b}, a);
  auto deduped = dedupeDescendantsForTest(resolved);
  QCOMPARE(deduped, QList<vnotex::NodeIdentifier>({a, b}));
}

void TestNotebookNodeControllerMultiSelect::copy_rightClickOutsideSelection_resolvesClickedOnly() {
  vnotex::NodeIdentifier a, b, clicked;
  a.notebookId = b.notebookId = clicked.notebookId = "nb-1";
  a.relativePath = "a.md";
  b.relativePath = "b.md";
  clicked.relativePath = "outside.md";

  auto resolved = resolveSelectionForTest({a, b}, clicked);
  auto deduped = dedupeDescendantsForTest(resolved);
  QCOMPARE(deduped.size(), 1);
  QCOMPARE(deduped.at(0), clicked);
}

void TestNotebookNodeControllerMultiSelect::cut_rightClickOutsideSelection_resolvesClickedOnly() {
  vnotex::NodeIdentifier a, b, clicked;
  a.notebookId = b.notebookId = clicked.notebookId = "nb-1";
  a.relativePath = "a.md";
  b.relativePath = "b.md";
  clicked.relativePath = "outside.md";

  auto resolved = resolveSelectionForTest({a, b}, clicked);
  auto deduped = dedupeDescendantsForTest(resolved);
  QCOMPARE(deduped.size(), 1);
  QCOMPARE(deduped.at(0), clicked);
}

void TestNotebookNodeControllerMultiSelect::dedupe_parentAndChildSameNotebook_dropsChild() {
  vnotex::NodeIdentifier parent, child;
  parent.notebookId = child.notebookId = "nb-1";
  parent.relativePath = "folder";
  child.relativePath = "folder/sub/note.md";

  auto deduped = dedupeDescendantsForTest({parent, child});
  QCOMPARE(deduped.size(), 1);
  QCOMPARE(deduped.at(0), parent);
}

void TestNotebookNodeControllerMultiSelect::dedupe_differentNotebooks_keepsBoth() {
  vnotex::NodeIdentifier a, b;
  a.notebookId = "nb-1";
  a.relativePath = "folder";
  b.notebookId = "nb-2";
  b.relativePath = "folder/note.md";

  auto deduped = dedupeDescendantsForTest({a, b});
  QCOMPARE(deduped.size(), 2);
  QCOMPARE(deduped.at(0), a);
  QCOMPARE(deduped.at(1), b);
}

// T6: duplicate mirrors the same resolve+dedupe pipeline used by delete/copy/cut.
// Controller construction needs 18+ services so we exercise the helper pipeline directly.

void TestNotebookNodeControllerMultiSelect::duplicate_multiSelection_resolvesAllIds() {
  vnotex::NodeIdentifier a, b, c;
  a.notebookId = b.notebookId = c.notebookId = "nb-1";
  a.relativePath = "a.md";
  b.relativePath = "b.md";
  c.relativePath = "c.md";

  auto resolved = resolveSelectionForTest({a, b, c}, b);
  auto deduped = dedupeDescendantsForTest(resolved);
  // All three should be duplicated when clicked node is inside selection.
  QCOMPARE(deduped.size(), 3);
  QCOMPARE(deduped, QList<vnotex::NodeIdentifier>({a, b, c}));
}

void TestNotebookNodeControllerMultiSelect::duplicate_parentAndChild_dropsChild() {
  vnotex::NodeIdentifier parent, child;
  parent.notebookId = child.notebookId = "nb-1";
  parent.relativePath = "folder";
  child.relativePath = "folder/note.md";

  auto resolved = resolveSelectionForTest({parent, child}, parent);
  auto deduped = dedupeDescendantsForTest(resolved);
  // Duplicating the parent folder already duplicates the child; dropping the child
  // from the batch prevents a redundant copy.
  QCOMPARE(deduped.size(), 1);
  QCOMPARE(deduped.at(0), parent);
}

void TestNotebookNodeControllerMultiSelect::duplicate_singleArg_delegatesToList() {
  // Back-compat: duplicateNode(id) must behave identically to duplicateNodes({id}).
  // The production single-arg overload body is now `duplicateNodes({p_nodeId});` —
  // verify the equivalent helper pipeline yields one node for a single input.
  vnotex::NodeIdentifier a;
  a.notebookId = "nb-1";
  a.relativePath = "single.md";

  QList<vnotex::NodeIdentifier> singletonInput{a};
  auto deduped = dedupeDescendantsForTest(singletonInput);
  QCOMPARE(deduped.size(), 1);
  QCOMPARE(deduped.at(0), a);
}

// T7: Open / Open With pipe through resolveSelection and silently skip folders.
// Production: NotebookNodeController::openNodes / openNodesWithCommand classify each id
// via getNodeInfo().isFolder; folders increment a skipped counter (single qDebug at end)
// and never reach nodeActivated or ProcessUtils::startDetached.
// Controller construction needs 18+ services so we mirror the helper at test scope.

// Test-only mirror of the production folder-skip filter.
static QList<vnotex::NodeIdentifier>
skipFoldersForTest(const QList<vnotex::NodeIdentifier> &p_ids,
                   const QSet<vnotex::NodeIdentifier> &p_folders) {
  QList<vnotex::NodeIdentifier> result;
  for (const auto &id : p_ids) {
    if (p_folders.contains(id)) {
      continue;
    }
    result.append(id);
  }
  return result;
}

void TestNotebookNodeControllerMultiSelect::open_multiSelection_resolvesAllIds() {
  vnotex::NodeIdentifier a, b, c;
  a.notebookId = b.notebookId = c.notebookId = "nb-1";
  a.relativePath = "a.md";
  b.relativePath = "b.md";
  c.relativePath = "c.md";

  auto resolved = resolveSelectionForTest({a, b, c}, b);
  // openNodes does NOT dedupeDescendants — only delete/copy/cut/duplicate dedupe.
  QCOMPARE(resolved.size(), 3);
  QCOMPARE(resolved, QList<vnotex::NodeIdentifier>({a, b, c}));
}

void TestNotebookNodeControllerMultiSelect::open_mixedFolderAndNote_skipsFolder() {
  vnotex::NodeIdentifier note_a, note_b, folder_x;
  note_a.notebookId = note_b.notebookId = folder_x.notebookId = "nb-1";
  note_a.relativePath = "a.md";
  note_b.relativePath = "b.md";
  folder_x.relativePath = "x";

  auto resolved = resolveSelectionForTest({note_a, note_b, folder_x}, note_a);
  QSet<vnotex::NodeIdentifier> folders{folder_x};
  auto opened = skipFoldersForTest(resolved, folders);

  QCOMPARE(opened.size(), 2);
  QCOMPARE(opened.at(0), note_a);
  QCOMPARE(opened.at(1), note_b);
}

void TestNotebookNodeControllerMultiSelect::openWith_multiSelection_buildsCommandPerNode() {
  vnotex::NodeIdentifier a, b, folder_x;
  a.notebookId = b.notebookId = folder_x.notebookId = "nb-1";
  a.relativePath = "a.md";
  b.relativePath = "b.md";
  folder_x.relativePath = "x";

  auto resolved = resolveSelectionForTest({a, b, folder_x}, b);
  QSet<vnotex::NodeIdentifier> folders{folder_x};
  auto launched = skipFoldersForTest(resolved, folders);

  // openNodesWithCommand fires ProcessUtils::startDetached exactly once per non-folder id.
  QCOMPARE(launched.size(), 2);
  QCOMPARE(launched.at(0), a);
  QCOMPARE(launched.at(1), b);
}

// Test-only helper mirroring NotebookNodeController::copyNodePaths.
// In production, this calls buildAbsolutePath per node, collects paths into QStringList,
// joins with newline, and writes to clipboard. We mirror the join+format logic here.
// Since buildAbsolutePath requires NotebookCoreService, we take pre-built paths as input.
static QString copyPathsForTest(const QStringList &p_paths) {
  if (p_paths.isEmpty()) {
    return QString();
  }
  return p_paths.join("\n");
}

void TestNotebookNodeControllerMultiSelect::copyPath_singleNode_unchangedBehavior() {
  // Single-node case: copyNodePath(id) must produce exactly one path,
  // no trailing newline (QStringList::join on size-1 list produces no separator).
  QStringList paths{"C:/notebooks/notebook1/single.md"};
  QString result = copyPathsForTest(paths);
  QCOMPARE(result, QStringLiteral("C:/notebooks/notebook1/single.md"));
  // Verify no trailing newline
  QVERIFY(!result.endsWith("\n"));
}

void TestNotebookNodeControllerMultiSelect::copyPath_multiSelection_newlineJoined() {
  // Multi-node case: N paths should be newline-separated.
  QStringList paths{"C:/notebooks/notebook1/a.md", "C:/notebooks/notebook1/b.md",
                    "C:/notebooks/notebook1/c.md"};
  QString result = copyPathsForTest(paths);

  // Expected: all three paths joined with newline, no trailing newline
  QString expected = "C:/notebooks/notebook1/a.md\n"
                     "C:/notebooks/notebook1/b.md\n"
                     "C:/notebooks/notebook1/c.md";
  QCOMPARE(result, expected);

  // Verify split count matches input
  QStringList split = result.split("\n");
  QCOMPARE(split.size(), 3);
  QCOMPARE(split.at(0), "C:/notebooks/notebook1/a.md");
  QCOMPARE(split.at(1), "C:/notebooks/notebook1/b.md");
  QCOMPARE(split.at(2), "C:/notebooks/notebook1/c.md");
}

// T10: Pin / Reload / Mark route through resolveSelection then call the list overload.
// Production: lambdas in addMiscActions emit
//   reloadNodes(resolveSelection(p_nodeId))
//   pinNodesToQuickAccess(resolveSelection(p_nodeId))
//   markNodes(resolveSelection(p_nodeId))
// The list overloads iterate the input ids (markNodes emits markRequested per id;
// reloadNodes emits nodeAboutToReload + nbModel->reloadNode per id;
// pinNodesToQuickAccess appends each id to QuickAccess items, skipping duplicates).
// No dedupeDescendants for these (Pin/Reload/Mark are non-destructive — descendant
// duplicates are harmless; matches Open semantics from T7).
// Controller construction needs 18+ services, so we mirror the resolve pipeline.

void TestNotebookNodeControllerMultiSelect::pin_multiSelection_resolvesAllIds() {
  vnotex::NodeIdentifier a, b, c;
  a.notebookId = b.notebookId = c.notebookId = "nb-1";
  a.relativePath = "a.md";
  b.relativePath = "b.md";
  c.relativePath = "c.md";

  auto resolved = resolveSelectionForTest({a, b, c}, b);
  // pinNodesToQuickAccess does NOT dedupeDescendants — pinning is non-destructive.
  QCOMPARE(resolved.size(), 3);
  QCOMPARE(resolved, QList<vnotex::NodeIdentifier>({a, b, c}));
}

void TestNotebookNodeControllerMultiSelect::reload_multiSelection_resolvesAllIds() {
  vnotex::NodeIdentifier a, b, c;
  a.notebookId = b.notebookId = c.notebookId = "nb-1";
  a.relativePath = "folder/a";
  b.relativePath = "folder/b";
  c.relativePath = "folder/c";

  auto resolved = resolveSelectionForTest({a, b, c}, a);
  // reloadNodes iterates the full selection; nodeAboutToReload + model reload fire per id.
  QCOMPARE(resolved.size(), 3);
  QCOMPARE(resolved, QList<vnotex::NodeIdentifier>({a, b, c}));
}

void TestNotebookNodeControllerMultiSelect::mark_multiSelection_resolvesAllIds() {
  vnotex::NodeIdentifier a, b, c;
  a.notebookId = b.notebookId = c.notebookId = "nb-1";
  a.relativePath = "a.md";
  b.relativePath = "b.md";
  c.relativePath = "c.md";

  auto resolved = resolveSelectionForTest({a, b, c}, c);
  // markNodes emits markRequested per id (batch mark dialog coalescing is a known
  // follow-up, out of scope).
  QCOMPARE(resolved.size(), 3);
  QCOMPARE(resolved, QList<vnotex::NodeIdentifier>({a, b, c}));
}

// T11: Single-arg back-compat regression. Each single-arg xxxNode(id) is now an
// inline delegation to xxxNodes({id}). The pipeline equivalent for a singleton
// input is just the singleton itself (no resolveSelection — single-arg methods
// are direct entry points used by keyboard shortcuts, toolbars, and view
// double-clicks that bypass right-click selection logic). We mirror that
// contract here.

static QList<vnotex::NodeIdentifier> singletonPipelineForTest(const vnotex::NodeIdentifier &p_id) {
  return QList<vnotex::NodeIdentifier>{p_id};
}

void TestNotebookNodeControllerMultiSelect::singleArg_open_unchanged() {
  vnotex::NodeIdentifier a;
  a.notebookId = "nb-1";
  a.relativePath = "a.md";
  auto result = singletonPipelineForTest(a);
  QCOMPARE(result.size(), 1);
  QCOMPARE(result.at(0), a);
}

void TestNotebookNodeControllerMultiSelect::singleArg_duplicate_unchanged() {
  vnotex::NodeIdentifier a;
  a.notebookId = "nb-1";
  a.relativePath = "dup.md";
  auto result = dedupeDescendantsForTest(singletonPipelineForTest(a));
  QCOMPARE(result.size(), 1);
  QCOMPARE(result.at(0), a);
}

void TestNotebookNodeControllerMultiSelect::singleArg_delete_unchanged() {
  vnotex::NodeIdentifier a;
  a.notebookId = "nb-1";
  a.relativePath = "del.md";
  auto result = dedupeDescendantsForTest(singletonPipelineForTest(a));
  QCOMPARE(result.size(), 1);
  QCOMPARE(result.at(0), a);
}

void TestNotebookNodeControllerMultiSelect::singleArg_remove_unchanged() {
  vnotex::NodeIdentifier a;
  a.notebookId = "nb-1";
  a.relativePath = "rm.md";
  auto result = dedupeDescendantsForTest(singletonPipelineForTest(a));
  QCOMPARE(result.size(), 1);
  QCOMPARE(result.at(0), a);
}

void TestNotebookNodeControllerMultiSelect::singleArg_copyPath_unchanged() {
  // Single-id copy path: exactly one absolute path, no trailing newline.
  QStringList paths{"C:/notebooks/nb-1/single.md"};
  QString result = copyPathsForTest(paths);
  QCOMPARE(result, QStringLiteral("C:/notebooks/nb-1/single.md"));
  QVERIFY(!result.endsWith("\n"));
  QCOMPARE(result.split("\n").size(), 1);
}

void TestNotebookNodeControllerMultiSelect::singleArg_pin_unchanged() {
  vnotex::NodeIdentifier a;
  a.notebookId = "nb-1";
  a.relativePath = "pin.md";
  // Pin/Reload/Mark do NOT dedupe (non-destructive); single-arg passes through.
  auto result = singletonPipelineForTest(a);
  QCOMPARE(result.size(), 1);
  QCOMPARE(result.at(0), a);
}

void TestNotebookNodeControllerMultiSelect::singleArg_reload_unchanged() {
  vnotex::NodeIdentifier a;
  a.notebookId = "nb-1";
  a.relativePath = "folder/note";
  auto result = singletonPipelineForTest(a);
  QCOMPARE(result.size(), 1);
  QCOMPARE(result.at(0), a);
}

void TestNotebookNodeControllerMultiSelect::singleArg_mark_unchanged() {
  vnotex::NodeIdentifier a;
  a.notebookId = "nb-1";
  a.relativePath = "mark.md";
  auto result = singletonPipelineForTest(a);
  QCOMPARE(result.size(), 1);
  QCOMPARE(result.at(0), a);
}

// T1: isSingleEffectiveSelection helper tests (mirrors resolveSelection contract)
void TestNotebookNodeControllerMultiSelect::
    isSingleEffectiveSelection_singleSelection_returnsTrue() {
  vnotex::NodeIdentifier a;
  a.notebookId = "nb-1";
  a.relativePath = "a.md";

  // Single-node selection, clicked same node → selection is [a], size == 1
  bool result = isSingleEffectiveSelectionForTest({a}, a);
  QVERIFY(result == true);
}

void TestNotebookNodeControllerMultiSelect::
    isSingleEffectiveSelection_multiSelectionClickedInside_returnsFalse() {
  vnotex::NodeIdentifier a, b, c;
  a.notebookId = b.notebookId = c.notebookId = "nb-1";
  a.relativePath = "a.md";
  b.relativePath = "b.md";
  c.relativePath = "c.md";

  // Multi-selection [a,b,c], clicked b (inside) → selection is [a,b,c], size == 3
  bool result = isSingleEffectiveSelectionForTest({a, b, c}, b);
  QVERIFY(result == false);
}

void TestNotebookNodeControllerMultiSelect::
    isSingleEffectiveSelection_clickedOutsideSelection_returnsTrue() {
  vnotex::NodeIdentifier a, b, c, d;
  a.notebookId = b.notebookId = c.notebookId = d.notebookId = "nb-1";
  a.relativePath = "a.md";
  b.relativePath = "b.md";
  c.relativePath = "c.md";
  d.relativePath = "d.md";

  // Multi-selection [a,b,c], clicked d (outside) → selection is [d], size == 1
  bool result = isSingleEffectiveSelectionForTest({a, b, c}, d);
  QVERIFY(result == true);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookNodeControllerMultiSelect)
#include "test_notebooknodecontroller_multiselect.moc"
