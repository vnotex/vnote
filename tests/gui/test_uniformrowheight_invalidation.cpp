// Asserts the Qt behaviour that VNote relies on instead of hand-rolled cache
// invalidation: an item view with uniform row heights / uniform item sizes must
// resample its row height when the stylesheet changes at runtime.
//
// QAbstractItemView::event() handles QEvent::StyleChange by calling the virtual
// doItemsLayout(); QTreeView::doItemsLayout() rebuilds its view items (so
// defaultItemHeight is resampled) and QListView clears its cached item size on
// relayout. VNote reaches that path by re-applying the application stylesheet on
// a theme switch (MainWindow2).
//
// This test exists so that if a future Qt ever stops doing it, the failure names
// the cause instead of surfacing as "rows do not resize until restart". Only
// then should a changeEvent() -> scheduleDelayedItemsLayout() workaround be added
// to the dock views.

#include <QtTest>

#include <QListView>
#include <QStandardItemModel>
#include <QTreeView>

namespace tests {

class TestUniformRowHeightInvalidation : public QObject {
  Q_OBJECT

private slots:
  void treeViewResamplesRowHeight();
  void listViewResamplesItemHeight();

private:
  static void fill(QStandardItemModel &p_model);
};

void TestUniformRowHeightInvalidation::fill(QStandardItemModel &p_model) {
  for (int i = 0; i < 5; ++i) {
    p_model.appendRow(new QStandardItem(QStringLiteral("Row %1").arg(i)));
  }
}

void TestUniformRowHeightInvalidation::treeViewResamplesRowHeight() {
  QStandardItemModel model;
  fill(model);

  QTreeView view;
  view.setUniformRowHeights(true);
  view.setModel(&model);
  view.setStyleSheet(QStringLiteral("QTreeView::item { padding: 2px 8px; }"));
  view.resize(300, 200);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));

  const int tight = view.visualRect(model.index(0, 0)).height();
  QVERIFY(tight > 0);

  view.setStyleSheet(QStringLiteral("QTreeView::item { padding: 12px 8px; }"));
  QCoreApplication::processEvents();

  const int loose = view.visualRect(model.index(0, 0)).height();
  QVERIFY2(loose > tight,
           qPrintable(QStringLiteral("uniform row height was not resampled on StyleChange: "
                                     "%1 -> %2")
                          .arg(tight)
                          .arg(loose)));
}

void TestUniformRowHeightInvalidation::listViewResamplesItemHeight() {
  QStandardItemModel model;
  fill(model);

  QListView view;
  view.setUniformItemSizes(true);
  view.setModel(&model);
  view.setStyleSheet(QStringLiteral("QListView::item { padding: 2px 8px; }"));
  view.resize(300, 200);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));

  const int tight = view.visualRect(model.index(0, 0)).height();
  QVERIFY(tight > 0);

  view.setStyleSheet(QStringLiteral("QListView::item { padding: 12px 8px; }"));
  QCoreApplication::processEvents();

  const int loose = view.visualRect(model.index(0, 0)).height();
  QVERIFY2(loose > tight,
           qPrintable(QStringLiteral("uniform item size was not resampled on StyleChange: "
                                     "%1 -> %2")
                          .arg(tight)
                          .arg(loose)));
}

} // namespace tests

QTEST_MAIN(tests::TestUniformRowHeightInvalidation)
#include "test_uniformrowheight_invalidation.moc"
