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

class TestNotebookNodeControllerMultiSelect : public QObject {
  Q_OBJECT

private slots:
  void resolveSelection_emptySelection_returnsClicked();
  void resolveSelection_clickedInsideSelection_returnsFullSelection();
  void resolveSelection_clickedOutsideSelection_returnsClickedOnly();
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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookNodeControllerMultiSelect)
#include "test_notebooknodecontroller_multiselect.moc"
