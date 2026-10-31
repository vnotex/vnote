// Tests for TreeHorizontalScrollHelper: column 0 of a QTreeView is sized to
// max(viewport width, content width), so a horizontal scrollbar appears for long
// content while short content still covers the whole viewport width.

#include <QtTest>

#include <QHeaderView>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTreeView>

#include <gui/utils/widgetutils.h>

namespace tests {

class TestTreeHorizontalScroll : public QObject {
  Q_OBJECT

private slots:
  void testShortContentCoversViewport();
  void testLongContentEnablesScrollbar();
  void testModelSwapIsHandled();
  void testResizeModeBecomesInteractive();

private:
  static QStandardItemModel *createModel(QObject *p_parent, const QString &p_text, int p_rows = 3);
};

QStandardItemModel *TestTreeHorizontalScroll::createModel(QObject *p_parent, const QString &p_text,
                                                          int p_rows) {
  auto *model = new QStandardItemModel(p_parent);
  for (int i = 0; i < p_rows; ++i) {
    model->appendRow(new QStandardItem(QStringLiteral("%1 %2").arg(p_text).arg(i)));
  }
  return model;
}

void TestTreeHorizontalScroll::testShortContentCoversViewport() {
  QTreeView view;
  view.setHeaderHidden(true);
  vnotex::WidgetUtils::showHorizontalScrollbar(&view);
  view.setModel(createModel(&view, QStringLiteral("ab")));
  view.resize(600, 200);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  QTRY_VERIFY(view.columnWidth(0) >= view.viewport()->width());

  // A few pixels inside the right edge of the viewport must still resolve to a row.
  const QRect vpRect = view.viewport()->rect();
  const QPoint pt(vpRect.right() - 3, view.visualRect(view.model()->index(0, 0)).center().y());
  QVERIFY(view.indexAt(pt).isValid());
  QCOMPARE(view.horizontalScrollBar()->maximum(), 0);
}

void TestTreeHorizontalScroll::testLongContentEnablesScrollbar() {
  QTreeView view;
  view.setHeaderHidden(true);
  vnotex::WidgetUtils::showHorizontalScrollbar(&view);
  view.setModel(createModel(&view, QString(400, QLatin1Char('W'))));
  view.resize(300, 200);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));

  QTRY_VERIFY(view.columnWidth(0) > view.viewport()->width());
  QTRY_VERIFY(view.horizontalScrollBar()->maximum() > 0);
}

void TestTreeHorizontalScroll::testModelSwapIsHandled() {
  QTreeView view;
  view.setHeaderHidden(true);
  vnotex::WidgetUtils::showHorizontalScrollbar(&view);
  view.setModel(createModel(&view, QStringLiteral("ab")));
  view.resize(300, 200);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  QTRY_VERIFY(view.columnWidth(0) >= view.viewport()->width());
  QCOMPARE(view.horizontalScrollBar()->maximum(), 0);

  // Swap in a model with much wider content.
  view.setModel(createModel(&view, QString(400, QLatin1Char('W'))));

  QTRY_VERIFY(view.horizontalScrollBar()->maximum() > 0);
}

void TestTreeHorizontalScroll::testResizeModeBecomesInteractive() {
  QTreeView view;
  view.setHeaderHidden(true);
  vnotex::WidgetUtils::showHorizontalScrollbar(&view);
  view.setModel(createModel(&view, QString(400, QLatin1Char('W'))));
  // A resize policy on the section would fight the helper's setColumnWidth().
  view.header()->setSectionResizeMode(0, QHeaderView::Stretch);
  view.resize(400, 200);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));

  QTRY_VERIFY(view.columnWidth(0) > view.viewport()->width());
  QTRY_VERIFY(view.horizontalScrollBar()->maximum() > 0);
}

} // namespace tests

QTEST_MAIN(tests::TestTreeHorizontalScroll)
#include "test_treehorizontalscroll.moc"
