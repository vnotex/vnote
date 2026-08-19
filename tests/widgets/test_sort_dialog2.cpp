// Tests for SortDialog2 — T4 of notebook-explorer-drag-reorder plan, extended
// by the sort-dialog-columns plan.
//
// SortDialog2 is a pure-UI modal dialog for reordering a flat list of entries
// (either folders OR files; the caller decides which list to pass). It returns
// the final order via getSortedOrder() after exec() == Accepted.
//
// Design notes:
//   - The dialog has NO ServiceLocator / vxcore dependency by contract; it
//     accepts the initial order, lets the user reshuffle, and returns the
//     final order. Persistence is the caller's responsibility.
//   - Widgets are discovered by objectName (per src/widgets/dialogs/AGENTS.md
//     "Test-discovery rule"). The names are stable contract surface; the
//     widget TYPE is not, and it changed from QListWidget to QTreeWidget when
//     the Name/Created/Modified columns landed.
//   - Rows now carry three columns. The raw name lives in
//     data(0, Qt::UserRole), which is what getSortedOrder() reads.
//   - Header clicks perform a ONE-SHOT reorder; sorting is never enabled on the
//     widget (setSortingEnabled(false) is explicit in the dialog).
//   - The old "internal drag-drop simulation" test drove
//     QListModel::moveRows directly. QTreeWidget's internal model does not
//     support moveRows, and a real QDrag-based drag cannot be driven from a
//     unit test (QDrag::exec spins a blocking native loop on Windows). The
//     drag contract is therefore asserted structurally: rows are drag-enabled
//     and NOT drop-enabled, only the invisible root accepts drops, and
//     overwrite mode is off — so a drop can never turn a row into a child.

#include <algorithm>

#include <QApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVector>
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

QTreeWidget *list(SortDialog2 &p_dlg) {
  return p_dlg.findChild<QTreeWidget *>(QLatin1String(kListWidgetName));
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

// Build a plain entry list with no timestamps (the ordering tests do not care).
QVector<SortDialog2::Entry> entries(const QStringList &p_names) {
  QVector<SortDialog2::Entry> out;
  out.reserve(p_names.size());
  for (const auto &name : p_names) {
    SortDialog2::Entry e;
    e.m_name = name;
    out.append(e);
  }
  return out;
}

SortDialog2::Entry entry(const QString &p_name, qint64 p_createdMs, qint64 p_modifiedMs) {
  SortDialog2::Entry e;
  e.m_name = p_name;
  if (p_createdMs > 0) {
    e.m_createdUtc = QDateTime::fromMSecsSinceEpoch(p_createdMs, Qt::UTC);
  }
  if (p_modifiedMs > 0) {
    e.m_modifiedUtc = QDateTime::fromMSecsSinceEpoch(p_modifiedMs, Qt::UTC);
  }
  return e;
}

// Simulate a user click on a header section. Emitting another object's signal
// directly is not portable (signals are protected under Qt 5), so go through
// the meta-object.
void clickHeader(QTreeWidget *p_list, int p_column) {
  QMetaObject::invokeMethod(p_list->header(), "sectionClicked", Qt::DirectConnection,
                            Q_ARG(int, p_column));
}

// Select a single row (clearing any prior selection).
void selectRow(QTreeWidget *p_list, int p_row) {
  p_list->clearSelection();
  p_list->setCurrentItem(p_list->topLevelItem(p_row));
}

// Select a contiguous range [first..last] (inclusive). Avoid the default
// setCurrentItem command, which in ExtendedSelection mode is ClearAndSelect
// and would wipe out the multi-selection we just established. Use NoUpdate so
// the current item moves without touching the selection state.
void selectRange(QTreeWidget *p_list, int p_first, int p_last) {
  p_list->clearSelection();
  for (int r = p_first; r <= p_last; ++r) {
    p_list->topLevelItem(r)->setSelected(true);
  }
  p_list->setCurrentItem(p_list->topLevelItem(p_first), 0, QItemSelectionModel::NoUpdate);
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

  // 6. Internal-move drag contract, asserted structurally (see file header).
  void testInternalDragDropContract();

  // 7. Multi-select: rows [0,1] of ["a","b","c","d"] + Down -> ["c","a","b","d"].
  void testMultiSelectMoveDown();

  // 8. Buttons enabled/disabled per selection.
  void testButtonsEnabledState();

  // 9. Three columns with the documented headers, and sorting stays disabled.
  void testColumnsAndHeaders();

  // 10. Header click on Name: first click ascending, second descending.
  void testHeaderSortByName();

  // 11. Header click on Modified orders by the timestamp key; unknown
  //     timestamps sort LAST in both directions.
  void testHeaderSortByModified();

  // 12. After a header sort, the move buttons still work.
  void testMoveAfterHeaderSort();

  // 13. A move-button move clears the sort indicator.
  void testMoveClearsSortIndicator();

  // 14. getSortedOrder() reads the role, not the displayed text.
  void testGetSortedOrderReadsRole();

  // 15. Contiguity guard still behaves with 3 columns / row selection.
  void testNonContiguousWithColumns();

  // 16. The drop path's manualOrderChanged signal clears the sort indicator.
  void testManualOrderChangedClearsSortIndicator();

  // 17. A header sort preserves the selection across the take/insert cycle.
  void testHeaderSortPreservesSelection();
};

// =============================================================================
// Subtest 1: Move-down on first item of ["c","a","b"] -> ["a","c","b"]
// =============================================================================
void TestSortDialog2::testMoveDownFirstItem() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  entries({QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b")}));
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);
  QCOMPARE(lw->topLevelItemCount(), 3);

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
                  entries({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
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
                  entries({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
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
                  entries({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
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
// the API is only valid after exec() == Accepted.
// =============================================================================
void TestSortDialog2::testCancelReturnsRejected() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  entries({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));

  QSignalSpy rejectedSpy(&dlg, &QDialog::rejected);
  QVERIFY(rejectedSpy.isValid());

  dlg.reject();

  QCOMPARE(rejectedSpy.count(), 1);
  QCOMPARE(dlg.result(), static_cast<int>(QDialog::Rejected));
}

// =============================================================================
// Subtest 6: Internal-move drag contract.
// A real QDrag cannot be driven from a unit test, so we assert the invariants
// that make a bad drop impossible: rows are drag-enabled but NOT drop-enabled
// (so nothing can become a child), only the invisible root accepts drops (so a
// flat reorder still works), overwrite mode is off, and the mode is
// InternalMove with MoveAction as the default. A programmatic reorder using the
// same take/insert primitives the drop path uses keeps every item top-level.
// =============================================================================
void TestSortDialog2::testInternalDragDropContract() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  entries({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);

  QCOMPARE(lw->dragDropMode(), QAbstractItemView::InternalMove);
  QCOMPARE(lw->defaultDropAction(), Qt::MoveAction);
  QVERIFY(!lw->dragDropOverwriteMode());
  QVERIFY(lw->invisibleRootItem()->flags().testFlag(Qt::ItemIsDropEnabled));

  for (int i = 0; i < lw->topLevelItemCount(); ++i) {
    auto *item = lw->topLevelItem(i);
    QVERIFY2(item->flags().testFlag(Qt::ItemIsDragEnabled), "rows must be draggable");
    QVERIFY2(!item->flags().testFlag(Qt::ItemIsDropEnabled),
             "rows must NOT accept drops, or a drag could turn one into a child");
  }

  // Reorder the way the drop path does: take row 0 and append it.
  lw->addTopLevelItem(lw->takeTopLevelItem(0));

  QCOMPARE(lw->topLevelItemCount(), 3);
  for (int i = 0; i < lw->topLevelItemCount(); ++i) {
    QCOMPARE(lw->topLevelItem(i)->childCount(), 0);
  }

  dlg.accept();

  const QStringList expected{QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("a")};
  QCOMPARE(dlg.getSortedOrder(), expected);
}

// =============================================================================
// Subtest 7: Multi-select [0,1] of ["a","b","c","d"] + Down -> ["c","a","b","d"]
// =============================================================================
void TestSortDialog2::testMultiSelectMoveDown() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  entries({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                           QStringLiteral("d")}));
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);
  QCOMPARE(lw->topLevelItemCount(), 4);

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
                  entries({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
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
  lw->topLevelItem(0)->setSelected(true);
  lw->topLevelItem(2)->setSelected(true);
  QVERIFY(!top->isEnabled());
  QVERIFY(!up->isEnabled());
  QVERIFY(!down->isEnabled());
  QVERIFY(!bottom->isEnabled());
}

// =============================================================================
// Subtest 9: Three columns, documented headers, sorting NOT enabled.
// =============================================================================
void TestSortDialog2::testColumnsAndHeaders() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  QVector<SortDialog2::Entry>{entry(QStringLiteral("a"), 1000, 2000)});
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);

  QCOMPARE(lw->columnCount(), 3);
  QCOMPARE(lw->headerItem()->text(0), QStringLiteral("Name"));
  QCOMPARE(lw->headerItem()->text(1), QStringLiteral("Created"));
  QCOMPARE(lw->headerItem()->text(2), QStringLiteral("Modified"));

  // One-shot sorting is the locked decision: live sorting must stay off.
  QVERIFY(!lw->isSortingEnabled());
  QVERIFY(lw->header()->sectionsClickable());
  QVERIFY(lw->header()->isSortIndicatorShown());

  // Timestamp cells are populated, and the sort key is the raw UTC ms.
  auto *item = lw->topLevelItem(0);
  QVERIFY(!item->text(1).isEmpty());
  QVERIFY(!item->text(2).isEmpty());
  QCOMPARE(item->data(1, Qt::UserRole).toLongLong(), 1000LL);
  QCOMPARE(item->data(2, Qt::UserRole).toLongLong(), 2000LL);

  // An invalid timestamp renders empty and carries no sort key.
  SortDialog2 dlg2(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                   entries({QStringLiteral("x")}));
  auto *lw2 = list(dlg2);
  QVERIFY(lw2 != nullptr);
  QVERIFY(lw2->topLevelItem(0)->text(1).isEmpty());
  QVERIFY(lw2->topLevelItem(0)->text(2).isEmpty());
  QVERIFY(!lw2->topLevelItem(0)->data(1, Qt::UserRole).isValid());
}

// =============================================================================
// Subtest 10: Header click on Name sorts asc, second click desc.
// "alpha" vs "Alpha" and a non-ASCII name pin the collator behavior: the
// collator is case-INSENSITIVE, with a case-sensitive tie-break so the order
// stays total.
// =============================================================================
void TestSortDialog2::testHeaderSortByName() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  entries({QStringLiteral("delta"), QStringLiteral("alpha"),
                           QStringLiteral("Alpha"), QString::fromUtf8("Ähnlich")}));
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);

  clickHeader(lw, 0);

  QStringList asc = dlg.getSortedOrder();
  QCOMPARE(asc.size(), 4);
  QCOMPARE(lw->header()->sortIndicatorSection(), 0);
  QCOMPARE(lw->header()->sortIndicatorOrder(), Qt::AscendingOrder);

  // "alpha"/"Alpha" tie under the case-insensitive collator and are broken
  // case-sensitively, so they are adjacent and in a deterministic order.
  const int iAlphaLower = asc.indexOf(QStringLiteral("alpha"));
  const int iAlphaUpper = asc.indexOf(QStringLiteral("Alpha"));
  QVERIFY(iAlphaLower >= 0 && iAlphaUpper >= 0);
  QCOMPARE(qAbs(iAlphaLower - iAlphaUpper), 1);
  QVERIFY2(iAlphaUpper < iAlphaLower,
           "case-sensitive tie-break puts 'Alpha' before 'alpha' (uppercase sorts first)");
  // Both "alpha" forms precede "delta" under any sane collation.
  QVERIFY(qMax(iAlphaLower, iAlphaUpper) < asc.indexOf(QStringLiteral("delta")));

  // Second click on the same column flips to descending: the exact reverse.
  clickHeader(lw, 0);
  QStringList desc = dlg.getSortedOrder();
  QCOMPARE(lw->header()->sortIndicatorOrder(), Qt::DescendingOrder);
  std::reverse(asc.begin(), asc.end());
  QCOMPARE(desc, asc);
}

// =============================================================================
// Subtest 11: Header click on Modified orders by the timestamp key; unknown
// timestamps sort LAST in both directions (an unknown time is not "oldest").
// =============================================================================
void TestSortDialog2::testHeaderSortByModified() {
  QVector<SortDialog2::Entry> es;
  es.append(entry(QStringLiteral("mid"), 1, 2000));
  es.append(entry(QStringLiteral("unknown"), 1, 0)); // no modified timestamp
  es.append(entry(QStringLiteral("old"), 1, 1000));
  es.append(entry(QStringLiteral("new"), 1, 3000));

  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"), es);
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);

  clickHeader(lw, 2);
  QCOMPARE(lw->header()->sortIndicatorSection(), 2);
  QCOMPARE(lw->header()->sortIndicatorOrder(), Qt::AscendingOrder);
  QCOMPARE(dlg.getSortedOrder(), (QStringList{QStringLiteral("old"), QStringLiteral("mid"),
                                              QStringLiteral("new"), QStringLiteral("unknown")}));

  clickHeader(lw, 2);
  QCOMPARE(lw->header()->sortIndicatorOrder(), Qt::DescendingOrder);
  QCOMPARE(dlg.getSortedOrder(), (QStringList{QStringLiteral("new"), QStringLiteral("mid"),
                                              QStringLiteral("old"), QStringLiteral("unknown")}));

  // The Created column is a separate key: all four share it, so a stable sort
  // preserves the current (descending-by-modified) order.
  const QStringList before = dlg.getSortedOrder();
  clickHeader(lw, 1);
  QCOMPARE(dlg.getSortedOrder(), before);
}

// =============================================================================
// Subtest 12: After a header sort, the move buttons still work and
// getSortedOrder() reflects the moved order.
// =============================================================================
void TestSortDialog2::testMoveAfterHeaderSort() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  entries({QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b")}));
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);

  clickHeader(lw, 0);
  QCOMPARE(dlg.getSortedOrder(),
           (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));

  selectRow(lw, 2);
  auto *up = upBtn(dlg);
  QVERIFY(up->isEnabled());
  up->click();
  QCOMPARE(dlg.getSortedOrder(),
           (QStringList{QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b")}));

  selectRow(lw, 0);
  auto *down = downBtn(dlg);
  QVERIFY(down->isEnabled());
  down->click();
  QCOMPARE(dlg.getSortedOrder(),
           (QStringList{QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b")}));
}

// =============================================================================
// Subtest 13: A move-button move clears the sort indicator — the visible order
// is no longer the sorted one, so the indicator must not claim otherwise.
// =============================================================================
void TestSortDialog2::testMoveClearsSortIndicator() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  entries({QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b")}));
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);

  clickHeader(lw, 0);
  QCOMPARE(lw->header()->sortIndicatorSection(), 0);

  selectRow(lw, 2);
  auto *up = upBtn(dlg);
  QVERIFY(up->isEnabled());
  up->click();

  QCOMPARE(lw->header()->sortIndicatorSection(), -1);

  // A fresh click on Name starts ascending again (the toggle state was reset).
  clickHeader(lw, 0);
  QCOMPARE(lw->header()->sortIndicatorOrder(), Qt::AscendingOrder);
  QCOMPARE(dlg.getSortedOrder(),
           (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
}

// =============================================================================
// Subtest 14: getSortedOrder() reads data(0, Qt::UserRole), not text(0).
// =============================================================================
void TestSortDialog2::testGetSortedOrderReadsRole() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  entries({QStringLiteral("a"), QStringLiteral("b")}));
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);

  // Decorate the display text; the raw name in the role must still come back.
  lw->topLevelItem(0)->setText(0, QStringLiteral("a (decorated)"));

  dlg.accept();
  QCOMPARE(dlg.getSortedOrder(), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

// =============================================================================
// Subtest 15: Contiguity guard with three columns. Selection is row-based
// (SelectRows), so selecting any cell of a row selects the whole row and the
// row count — not the cell count — drives the guard.
// =============================================================================
void TestSortDialog2::testNonContiguousWithColumns() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  entries({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                           QStringLiteral("d")}));
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);
  QCOMPARE(lw->selectionBehavior(), QAbstractItemView::SelectRows);

  // Non-contiguous rows [0, 2]: buttons disabled AND the handlers refuse.
  lw->clearSelection();
  lw->topLevelItem(0)->setSelected(true);
  lw->topLevelItem(2)->setSelected(true);
  QCOMPARE(lw->selectionModel()->selectedRows().size(), 2);
  QVERIFY(!upBtn(dlg)->isEnabled());
  QVERIFY(!downBtn(dlg)->isEnabled());

  // A disabled button cannot be clicked, so the order is untouched.
  const QStringList before = dlg.getSortedOrder();
  downBtn(dlg)->click();
  QCOMPARE(dlg.getSortedOrder(), before);

  // The handlers ALSO re-check contiguity themselves, to cover invocation
  // paths that bypass the button's disabled state (keyboard shortcuts).
  // Force-enable the button to reach that guard.
  downBtn(dlg)->setEnabled(true);
  downBtn(dlg)->click();
  QCOMPARE(dlg.getSortedOrder(), before);
  upBtn(dlg)->setEnabled(true);
  upBtn(dlg)->click();
  QCOMPARE(dlg.getSortedOrder(), before);

  // Contiguous rows [1, 2] re-enable the buttons.
  selectRange(lw, 1, 2);
  QVERIFY(upBtn(dlg)->isEnabled());
  QVERIFY(downBtn(dlg)->isEnabled());
}

// =============================================================================
// Subtest 16: The drop path clears the sort indicator.
// A real QDrag cannot be driven here (see the file header), but the wiring
// between the tree subclass's manualOrderChanged signal and the dialog's
// clearSortIndicator() helper is exactly what a drop relies on, so raise the
// signal through the meta-object and assert the observable outcome.
// =============================================================================
void TestSortDialog2::testManualOrderChangedClearsSortIndicator() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  entries({QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b")}));
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);

  clickHeader(lw, 0);
  QCOMPARE(lw->header()->sortIndicatorSection(), 0);

  const int sigIdx = lw->metaObject()->indexOfSignal("manualOrderChanged()");
  QVERIFY2(sigIdx >= 0, "the sort tree must expose manualOrderChanged() — the drop path uses it");
  QVERIFY(QMetaObject::invokeMethod(lw, "manualOrderChanged", Qt::DirectConnection));

  QCOMPARE(lw->header()->sortIndicatorSection(), -1);

  // The toggle state was reset too: the next Name click starts ascending.
  clickHeader(lw, 0);
  QCOMPARE(lw->header()->sortIndicatorOrder(), Qt::AscendingOrder);
}

// =============================================================================
// Subtest 17: A header sort takes every row out and re-inserts it, so the
// selection must be restored by name afterwards (and the move buttons must
// reflect the block's NEW position).
// =============================================================================
void TestSortDialog2::testHeaderSortPreservesSelection() {
  SortDialog2 dlg(QStringLiteral("Sort"), QStringLiteral("Reorder"),
                  entries({QStringLiteral("d"), QStringLiteral("c"), QStringLiteral("b"),
                           QStringLiteral("a")}));
  auto *lw = list(dlg);
  QVERIFY(lw != nullptr);

  // Select ["d","c"] — rows 0..1, i.e. the top of the list.
  selectRange(lw, 0, 1);
  QVERIFY(!upBtn(dlg)->isEnabled());
  QVERIFY(downBtn(dlg)->isEnabled());

  clickHeader(lw, 0);
  QCOMPARE(dlg.getSortedOrder(), (QStringList{QStringLiteral("a"), QStringLiteral("b"),
                                              QStringLiteral("c"), QStringLiteral("d")}));

  // The same two names are still selected, now at rows 2..3.
  QStringList stillSelected;
  const auto rows = lw->selectionModel()->selectedRows();
  for (const auto &idx : rows) {
    stillSelected << lw->topLevelItem(idx.row())->data(0, Qt::UserRole).toString();
  }
  stillSelected.sort();
  QCOMPARE(stillSelected, (QStringList{QStringLiteral("c"), QStringLiteral("d")}));

  // Button state follows the block to the BOTTOM of the list.
  QVERIFY(upBtn(dlg)->isEnabled());
  QVERIFY(!downBtn(dlg)->isEnabled());
  QVERIFY(!bottomBtn(dlg)->isEnabled());
}

} // namespace tests

QTEST_MAIN(tests::TestSortDialog2)
#include "test_sort_dialog2.moc"
