// Tests for TreeViewUtils: dock/sidebar trees share this default indentation
// step unless their active theme opts into a different spacing policy.

#include <QtTest>

#include <QTreeView>

#include <gui/utils/treeviewutils.h>

namespace tests {

class TestTreeViewIndentation : public QObject {
  Q_OBJECT

private slots:
  void testAppliesSharedIndentation();
  void testNullViewIsNoOp();
};

void TestTreeViewIndentation::testAppliesSharedIndentation() {
  QTreeView view;
  vnotex::TreeViewUtils::applyIndentation(&view);
  QCOMPARE(view.indentation(), vnotex::TreeViewUtils::indentation());
}

void TestTreeViewIndentation::testNullViewIsNoOp() {
  vnotex::TreeViewUtils::applyIndentation(nullptr);
  QVERIFY(true);
}

} // namespace tests

QTEST_MAIN(tests::TestTreeViewIndentation)
#include "test_treeviewindentation.moc"
