#include <QtTest>

#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>

#include <gui/utils/treechildcountdelegate.h>

namespace tests {

class TestTreeChildCountDelegate : public QObject {
  Q_OBJECT

private slots:
  void viewportAnchorSurvivesScrollingAndResize();
  void countsFollowImmediateFilteredChildren();
  void roleCountsIgnoreFilteredChildren();
  void maximumScrollRevealsNameWithRoleCount();
  void deepRowsAndLargeCountsKeepTheirBadge();

private:
  static void prepare(QTreeView &p_view, QAbstractItemModel &p_model, int p_countRole = -1) {
    p_view.setModel(&p_model);
    p_view.setItemDelegate(new vnotex::TreeChildCountDelegate(&p_view, p_countRole));
    p_view.setHeaderHidden(true);
    p_view.header()->setStretchLastSection(false);
    p_view.setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    p_view.setFocusPolicy(Qt::NoFocus);
    p_view.setStyleSheet(QStringLiteral("QTreeView { color: black; background: white; }"
                                        "QTreeView::item { padding: 4px; }"));
    p_view.resize(320, 160);
    p_view.setColumnWidth(0, 1200);
    p_view.show();
  }

  static QImage badgeStrip(QTreeView &p_view, const QModelIndex &p_index, int p_width = 32) {
    const QRect row = p_view.visualRect(p_index);
    return p_view.viewport()
        ->grab(QRect(p_view.viewport()->width() - p_width, row.top(), p_width, row.height()))
        .toImage();
  }
};

void TestTreeChildCountDelegate::viewportAnchorSurvivesScrollingAndResize() {
  QStandardItemModel model;
  auto *folder = new QStandardItem(QString(100, QLatin1Char('W')));
  folder->setRowCount(3);
  model.appendRow(folder);
  QTreeView view;
  prepare(view, model);
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  const auto index = model.index(0, 0);
  QVERIFY(view.horizontalScrollBar()->maximum() > view.viewport()->width());
  const QImage initial = badgeStrip(view, index);

  view.horizontalScrollBar()->setValue(170);
  QCoreApplication::processEvents();
  QCOMPARE(badgeStrip(view, index), initial);
  view.horizontalScrollBar()->setValue(view.horizontalScrollBar()->maximum());
  QCoreApplication::processEvents();
  QCOMPARE(badgeStrip(view, index), initial);
  view.resize(460, 160);
  QCoreApplication::processEvents();
  QCOMPARE(badgeStrip(view, index), initial);

  const QPoint badgePoint(view.viewport()->width() - 18, view.visualRect(index).center().y());
  QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, badgePoint);
  QCOMPARE(view.currentIndex(), index);
}

void TestTreeChildCountDelegate::countsFollowImmediateFilteredChildren() {
  QStandardItemModel model;
  auto *folder = new QStandardItem(QStringLiteral("Folder"));
  model.appendRow(folder);
  QSortFilterProxyModel proxy;
  proxy.setRecursiveFilteringEnabled(true);
  proxy.setSourceModel(&model);
  QTreeView view;
  prepare(view, proxy);
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  auto index = proxy.index(0, 0);
  const QImage empty = badgeStrip(view, index);

  auto *child = new QStandardItem(QStringLiteral("match"));
  folder->appendRow(child);
  QCoreApplication::processEvents();
  const QImage one = badgeStrip(view, index);
  QVERIFY(one != empty);
  folder->appendRow(new QStandardItem(QStringLiteral("other")));
  QCoreApplication::processEvents();
  const QImage two = badgeStrip(view, index);
  QVERIFY(two != one);

  child->appendRow(new QStandardItem(QStringLiteral("grandchild")));
  QCoreApplication::processEvents();
  QCOMPARE(badgeStrip(view, index), two); // Immediate children, not descendants.
  view.expand(index);
  QCoreApplication::processEvents();
  QCOMPARE(badgeStrip(view, index), two); // Expansion does not change the count.
  proxy.setFilterFixedString(QStringLiteral("match"));
  QCoreApplication::processEvents();
  QCOMPARE(badgeStrip(view, proxy.index(0, 0)), one);
  proxy.setFilterFixedString(QString());
  folder->removeRows(0, folder->rowCount());
  QCoreApplication::processEvents();
  QCOMPARE(badgeStrip(view, proxy.index(0, 0)), empty);
}

void TestTreeChildCountDelegate::roleCountsIgnoreFilteredChildren() {
  constexpr int countRole = Qt::UserRole + 1;
  QStandardItemModel model;
  auto *tag = new QStandardItem(QStringLiteral("Tag"));
  tag->setData(7, countRole);
  tag->appendRow(new QStandardItem(QStringLiteral("match")));
  tag->appendRow(new QStandardItem(QStringLiteral("other")));
  model.appendRow(tag);
  QSortFilterProxyModel proxy;
  proxy.setRecursiveFilteringEnabled(true);
  proxy.setSourceModel(&model);
  QTreeView view;
  prepare(view, proxy, countRole);
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  const auto initial = badgeStrip(view, proxy.index(0, 0), 64);
  proxy.setFilterFixedString(QStringLiteral("match"));
  QCoreApplication::processEvents();
  QCOMPARE(badgeStrip(view, proxy.index(0, 0), 64), initial);
  proxy.setFilterFixedString(QString());
  tag->removeRows(0, tag->rowCount());
  QCoreApplication::processEvents();
  QCOMPARE(badgeStrip(view, proxy.index(0, 0), 64), initial);

  QStyleOptionViewItem option;
  option.initFrom(&view);
  option.widget = &view;
  const auto small = view.itemDelegate()->sizeHint(option, proxy.index(0, 0));
  tag->setData(12345, countRole);
  QCoreApplication::processEvents();
  const auto large = view.itemDelegate()->sizeHint(option, proxy.index(0, 0));
  QVERIFY(large.width() > small.width());
  QCOMPARE(large.height(), small.height());
  QVERIFY(badgeStrip(view, proxy.index(0, 0), 64) != initial);
  tag->setData(0, countRole);
  QCoreApplication::processEvents();
  const auto empty = badgeStrip(view, proxy.index(0, 0), 64);
  QVERIFY(empty != initial);
  tag->setData(-1, countRole);
  QCoreApplication::processEvents();
  QCOMPARE(badgeStrip(view, proxy.index(0, 0), 64), empty);
}

void TestTreeChildCountDelegate::maximumScrollRevealsNameWithRoleCount() {
  struct Delegate : vnotex::TreeChildCountDelegate {
    using TreeChildCountDelegate::childCountBadge;
    using TreeChildCountDelegate::initStyleOption;
    using TreeChildCountDelegate::TreeChildCountDelegate;
  };
  constexpr int countRole = Qt::UserRole + 1;
  QStandardItemModel model;
  const auto name = QStringLiteral("Long_") + QString(100, QLatin1Char('w'));
  auto *tag = new QStandardItem(name);
  model.appendRow(tag);
  QTreeView view;
  view.setHeaderHidden(true);
  view.header()->setStretchLastSection(false);
  view.setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  view.setModel(&model);
  auto *delegate = new Delegate(&view, countRole);
  view.setItemDelegate(delegate);
  view.resize(440, 160);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  for (const int count : {9, 10, 100}) {
    tag->setData(count, countRole);
    view.resizeColumnToContents(0);
    QCoreApplication::processEvents();
    view.horizontalScrollBar()->setValue(view.horizontalScrollBar()->maximum());
    QCoreApplication::processEvents();
    const auto index = model.index(0, 0);
    QStyleOptionViewItem option;
    option.initFrom(&view);
    option.widget = &view;
    option.rect = view.visualRect(index);
    delegate->initStyleOption(&option, index);
    const auto badge = delegate->childCountBadge(option, count);
    const auto textRect = vnotex::ItemViewUtils::textRect(option, view.style());
    const int available = badge.m_rect.left() - badge.m_gap - textRect.left();
    QCOMPARE(option.fontMetrics.elidedText(name, option.textElideMode, available), name);
  }
}

void TestTreeChildCountDelegate::deepRowsAndLargeCountsKeepTheirBadge() {
  QStandardItemModel model;
  auto *folder = new QStandardItem;
  folder->setRowCount(1234);
  model.appendRow(folder);
  QTreeView view;
  prepare(view, model);
  auto *delegate = static_cast<vnotex::TreeChildCountDelegate *>(view.itemDelegate());
  QStyleOptionViewItem option;
  option.initFrom(&view);
  option.widget = &view;
  option.rect = QRect(20, 0, 1180, 32);
  option.state &= ~(QStyle::State_HasFocus | QStyle::State_MouseOver);
  const auto index = model.index(0, 0);
  const auto render = [&]() {
    QImage image(view.viewport()->width(), option.rect.height(), QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    delegate->paint(&painter, option, index);
    return image;
  };
  const QImage emptyName = render();
  folder->setText(QString(150, QLatin1Char('W')));
  const QImage longName = render();
  QRect gutter;
  for (int y = 0; y < emptyName.height(); ++y) {
    for (int x = 0; x < emptyName.width(); ++x) {
      if (qGray(emptyName.pixel(x, y)) < 250) {
        gutter |= QRect(x, y, 1, 1);
      }
    }
  }
  QVERIFY(gutter.isValid()); // The count itself, not unrelated label pixels.
  gutter = gutter.adjusted(-2, 0, 2, 0);
  QCOMPARE(longName.copy(gutter), emptyName.copy(gutter));

  option.rect.setLeft(view.viewport()->width() + 100);
  QCOMPARE(render().copy(gutter), emptyName.copy(gutter));
  option.rect.setRight(option.rect.left() - 20); // Indentation exceeds the column.
  QCOMPARE(render().copy(gutter), emptyName.copy(gutter));

  delegate->setShowChildCount(false);
  const QImage hidden = render();
  QVERIFY(hidden.copy(gutter) != emptyName.copy(gutter));
}

} // namespace tests

QTEST_MAIN(tests::TestTreeChildCountDelegate)
#include "test_treechildcountdelegate.moc"
