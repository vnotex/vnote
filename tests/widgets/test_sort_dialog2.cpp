// Tests for SortDialog2 — T4 of notebook-explorer-drag-reorder plan.
//
// SortDialog2 is a pure-UI modal dialog for reordering a flat list of names
// (either folders OR files; the caller decides which list to pass). It returns
// the final order via getSortedOrder() after exec() == Accepted.
//
// Design notes:
//   - The dialog has NO ServiceLocator / vxcore dependency by contract; it
//     accepts the initial order, lets the user reshuffle, and returns the
//     final order. Persistence is the caller's responsibility.
//   - Widgets are discovered by objectName (per src/widgets/dialogs/AGENTS.md
//     "Test-discovery rule"). The names are stable contract surface.
//   - QListWidget's underlying QListModel in Qt 6.9 DOES implement moveRows,
//     so the "internal drag-drop simulation" test drives model()->moveRow
//     directly (same code path the InternalMove drag-drop machinery touches).

#include <QApplication>
#include <QDialogButtonBox>
#include <QItemSelectionModel>
#include <QListWidget>
#include <QPushButton>
#include <QtTest>

#include <widgets/dialogs/sortdialog2.h>

using namespace vnotex;

namespace tests {

namespace {

// These must match the object-name constants used inside sortdialog2.cpp.
const char *kListWidgetName = "sortListWidget";
const char *kTopBtnName = "sortTopBtn";
const char *kUpBtnName = "sortUpBtn";
const char *kDownBtnName = "sortDownBtn";
const char *kBottomBtnName = "sortBottomBtn";

QListWidget *list(SortDialog2 &p_dlg) {
  return p_dlg.findChild<QListWidget *>(QLatin1String(kListWidgetName));
}

QPushButton *topBtn(SortDialog2 &p_dlg) {
  return p_dlg.findChild<QPushButton *>(QLatin1String(kTopBtnName));
}

QPushButton *upBtn(SortDialog2 &p_dlg) {
  return p_dlg.findChild<QPushButton *>(QLatin1String(kUpBtnName));
}

QPushButton *downBtn(SortDialog2 &p_dlg) {
  return p_dlg.findChild<QPushButton *>(QLatin1String(kDownBtnName));
}

QPushButton *bottomBtn(SortDialog2 &p_dlg) {
  return p_dlg.findChild<QPushButton *>(QLatin1String(kBottomBtnName));
}

// Select a single row (clearing any prior selection).
void selectRow(QListWidget *p_list, int p_row) {
  p_list->clearSelection();
  p_list->setCurrentRow(p_row);
}

// Select a contiguous range [first..last] (inclusive). Avoid setCurrentRow,
// which in ExtendedSelection mode defaults to ClearAndSelect and would wipe
// out the multi-selection we just established. Use NoUpdate so the current
// item moves without touching the selection state.
void selectRange(QListWidget *p_list, int p_first, int p_last) {
  p_list->clearSelection();
  for (int r = p_first; r <= p_last; ++r) {
    p_list->item(r)->setSelected(true);
  }
  p_list->setCurrentItem(p_list->item(p_first), QItemSelectionModel::NoUpdate);
}

} // namespace

class TestSortDialog2 : public QObject {
  Q_OBJECT

private slots:
  // 1. Move-down on the first item: ["c","a","b"] -> ["a","c","b"].
  void testMoveDownFirstItem();

  // 2. Move-to-top on the last item: ["a","b","c"] -> ["c","a","b"].
  void testMoveToTopLastItem();

  // 3. Move-to-bottom on the first item: ["a","b","c"] -> ["b","c","a"].
  void testMoveToBottomFirstItem();

  // 4. Move-up on the second item: ["a","b","c"] -> ["b","a","c"].
  void testMoveUpSecondItem();

  // 5. Cancel returns Rejected; caller must NOT use getSortedOrder() on Rejected.
  //    Documents the contract by NOT calling getSortedOrder() after reject().
  void testCancelReturnsRejected();

  // 6. "Internal drag-drop simulation": reorder rows via takeItem/insertItem
  //    (same code path InternalMove uses on drop) and verify getSortedOrder().
  void testInternalDragDropSimulation();

  // 7. Multi-select: rows [0,1] of ["a","b","c","d"] + Down -> ["c","a","b","d"].
  void testMultiSelectMoveDown();

  // 8. Buttons enabled/disabled per selection:
  //      - first row selected -> Up + Top disabled, Down + Bottom enabled
  //      - last row selected  -> Down + Bottom disabled, Up + Top enabled
  //      - empty selection    -> all four disabled
  //      - non-contiguous     -> all four disabled
  void testButtonsEnabledState();
};

// =============================================================================
// Subtest 1: Move-down on first item of ["c","a","b"] -> ["a","c","b"]
// =============================================================================
void TestSortDialog2::testMoveDownFirstItem() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  QStringList{QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b")});
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);
  QCOMPARE(lw->count(), 3);

  selectRow(lw, 0);

  auto *down = downBtn(dlg);
  QVERIFY(down != nullptr);
  QVERIFY(down->isEnabled());
  down->click();

  // Simulate Accept by directly accepting (no QDialog::exec event loop in tests).
  dlg.accept();

  const QStringList expected{QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b")};
  QCOMPARE(dlg.getSortedOrder(), expected);
}

// =============================================================================
// Subtest 2: Move-to-top on last item of ["a","b","c"] -> ["c","a","b"]
// =============================================================================
void TestSortDialog2::testMoveToTopLastItem() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);

  selectRow(lw, 2);

  auto *top = topBtn(dlg);
  QVERIFY(top != nullptr);
  QVERIFY(top->isEnabled());
  top->click();

  dlg.accept();

  const QStringList expected{QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b")};
  QCOMPARE(dlg.getSortedOrder(), expected);
}

// =============================================================================
// Subtest 3: Move-to-bottom on first item of ["a","b","c"] -> ["b","c","a"]
// =============================================================================
void TestSortDialog2::testMoveToBottomFirstItem() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);

  selectRow(lw, 0);

  auto *bottom = bottomBtn(dlg);
  QVERIFY(bottom != nullptr);
  QVERIFY(bottom->isEnabled());
  bottom->click();

  dlg.accept();

  const QStringList expected{QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("a")};
  QCOMPARE(dlg.getSortedOrder(), expected);
}

// =============================================================================
// Subtest 4: Move-up on second item of ["a","b","c"] -> ["b","a","c"]
// =============================================================================
void TestSortDialog2::testMoveUpSecondItem() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);

  selectRow(lw, 1);

  auto *up = upBtn(dlg);
  QVERIFY(up != nullptr);
  QVERIFY(up->isEnabled());
  up->click();

  dlg.accept();

  const QStringList expected{QStringLiteral("b"), QStringLiteral("a"), QStringLiteral("c")};
  QCOMPARE(dlg.getSortedOrder(), expected);
}

// =============================================================================
// Subtest 5: Cancel returns Rejected.
// Note: We do NOT call getSortedOrder() after reject() — that is the contract:
// the API is only valid after exec() == Accepted. Calling it on Rejected is
// undefined behavior at the contract level (technically the list still
// contains the items, but the caller must not consume them).
// =============================================================================
void TestSortDialog2::testCancelReturnsRejected() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});

  // Hook the rejected() signal to record the result code; reject() should
  // produce QDialog::Rejected.
  QSignalSpy rejectedSpy(&dlg, &QDialog::rejected);
  QVERIFY(rejectedSpy.isValid());

  dlg.reject();

  QCOMPARE(rejectedSpy.count(), 1);
  QCOMPARE(dlg.result(), static_cast<int>(QDialog::Rejected));
}

// =============================================================================
// Subtest 6: Internal drag-drop simulation via model()->moveRow.
// Qt 6.9's QListModel implements moveRows, so this is the model-level
// counterpart of an InternalMove drop. Move row 0 to AFTER row 2 -> dest=3.
// =============================================================================
void TestSortDialog2::testInternalDragDropSimulation() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);

  // Qt destination semantics: to move row 0 to AFTER row 2, destination is
  // 3 (one past the current row 2). The model rewrites itself, and
  // getSortedOrder() walks the list top-to-bottom.
  const bool moved = lw->model()->moveRow(QModelIndex(), 0, QModelIndex(), 3);
  QVERIFY2(moved, "QListModel::moveRows must succeed on Qt 6.9. If this regresses, "
                  "fall back to lw->takeItem(0) + lw->insertItem(lw->count(), taken).");

  dlg.accept();

  const QStringList expected{QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("a")};
  QCOMPARE(dlg.getSortedOrder(), expected);
}

// =============================================================================
// Subtest 7: Multi-select [0,1] of ["a","b","c","d"] + Down -> ["c","a","b","d"]
// The block [a,b] moves together as a contiguous unit, preserving internal order.
// =============================================================================
void TestSortDialog2::testMultiSelectMoveDown() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                              QStringLiteral("d")});
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);
  QCOMPARE(lw->count(), 4);

  selectRange(lw, 0, 1);

  auto *down = downBtn(dlg);
  QVERIFY(down != nullptr);
  QVERIFY(down->isEnabled());
  down->click();

  dlg.accept();

  const QStringList expected{QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b"),
                             QStringLiteral("d")};
  QCOMPARE(dlg.getSortedOrder(), expected);
}

// =============================================================================
// Subtest 8: Button enabled state varies with selection.
// =============================================================================
void TestSortDialog2::testButtonsEnabledState() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
  auto *lw = list(dlg);
  auto *top = topBtn(dlg);
  auto *up = upBtn(dlg);
  auto *down = downBtn(dlg);
  auto *bottom = bottomBtn(dlg);
  QVERIFY(lw && top && up && down && bottom);

  // Empty selection: all four disabled.
  lw->clearSelection();
  QVERIFY(!top->isEnabled());
  QVERIFY(!up->isEnabled());
  QVERIFY(!down->isEnabled());
  QVERIFY(!bottom->isEnabled());

  // First row selected: Up + Top disabled; Down + Bottom enabled.
  selectRow(lw, 0);
  QVERIFY(!top->isEnabled());
  QVERIFY(!up->isEnabled());
  QVERIFY(down->isEnabled());
  QVERIFY(bottom->isEnabled());

  // Middle row selected: all four enabled.
  selectRow(lw, 1);
  QVERIFY(top->isEnabled());
  QVERIFY(up->isEnabled());
  QVERIFY(down->isEnabled());
  QVERIFY(bottom->isEnabled());

  // Last row selected: Down + Bottom disabled; Up + Top enabled.
  selectRow(lw, 2);
  QVERIFY(top->isEnabled());
  QVERIFY(up->isEnabled());
  QVERIFY(!down->isEnabled());
  QVERIFY(!bottom->isEnabled());

  // Non-contiguous selection: rows [0, 2]; all four disabled.
  lw->clearSelection();
  lw->item(0)->setSelected(true);
  lw->item(2)->setSelected(true);
  QVERIFY(!top->isEnabled());
  QVERIFY(!up->isEnabled());
  QVERIFY(!down->isEnabled());
  QVERIFY(!bottom->isEnabled());
}

} // namespace tests

QTEST_MAIN(tests::TestSortDialog2)
#include "test_sort_dialog2.moc"
