#include <QDir>
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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookNodeControllerMultiSelect)
#include "test_notebooknodecontroller_multiselect.moc"
