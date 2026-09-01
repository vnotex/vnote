// Tests for TreeViewUtils: every dock/sidebar tree must share one indentation
// step, so a level-2 item lines up across the Notebooks, Tags, Search, Outline
// and Tasks docks. The call sites themselves are covered by the grep gate
// tests/utils/test_tree_indentation_drift.cpp; this test covers the helper.

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
