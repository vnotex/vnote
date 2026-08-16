// Tests for NotebookNodeDelegate::sizeHint(): the width must be derived from the
// node name (and the child-count badge) so a content-sized column can show the
// full label instead of a hardcoded 200px.

#include <QtTest>

#include <QStandardItemModel>
#include <QStyleOptionViewItem>

#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <models/inodelistmodel.h>
#include <views/notebooknodedelegate.h>

namespace tests {

class TestNotebookNodeDelegateSizeHint : public QObject {
  Q_OBJECT

private slots:
  void testLongNameIsWiderThanLegacyFixedWidth();
  void testChildCountBadgeAddsWidth();

private:
  static QModelIndex makeIndex(QStandardItemModel &p_model, const vnotex::NodeInfo &p_info);
};

QModelIndex TestNotebookNodeDelegateSizeHint::makeIndex(QStandardItemModel &p_model,
                                                        const vnotex::NodeInfo &p_info) {
  auto *item = new QStandardItem(p_info.name);
  item->setData(QVariant::fromValue(p_info), vnotex::INodeListModel::NodeInfoRole);
  p_model.appendRow(item);
  return p_model.index(p_model.rowCount() - 1, 0);
}

void TestNotebookNodeDelegateSizeHint::testLongNameIsWiderThanLegacyFixedWidth() {
  vnotex::ServiceLocator services;
  vnotex::NotebookNodeDelegate delegate(services);

  QStandardItemModel model;
  vnotex::NodeInfo info;
  info.id.notebookId = QStringLiteral("nb");
  info.id.relativePath = QStringLiteral("a.md");
  info.isFolder = false;
  info.name = QString(120, QLatin1Char('W'));
  const QModelIndex idx = makeIndex(model, info);

  QStyleOptionViewItem opt;
  const QSize hint = delegate.sizeHint(opt, idx);

  // The old implementation always returned 200.
  QVERIFY(hint.width() > 200);
  QVERIFY(hint.height() > 0);
}

void TestNotebookNodeDelegateSizeHint::testChildCountBadgeAddsWidth() {
  vnotex::ServiceLocator services;
  vnotex::NotebookNodeDelegate delegate(services);
  QVERIFY(delegate.showChildCount());

  QStandardItemModel model;

  vnotex::NodeInfo plain;
  plain.id.notebookId = QStringLiteral("nb");
  plain.id.relativePath = QStringLiteral("MyFolder");
  plain.isFolder = true;
  plain.name = QStringLiteral("MyFolder");
  plain.childCount = 0;

  vnotex::NodeInfo badged = plain;
  badged.childCount = 42;

  QStyleOptionViewItem opt;
  const int plainWidth = delegate.sizeHint(opt, makeIndex(model, plain)).width();
  const int badgedWidth = delegate.sizeHint(opt, makeIndex(model, badged)).width();

  QVERIFY(badgedWidth > plainWidth);
}

} // namespace tests

QTEST_MAIN(tests::TestNotebookNodeDelegateSizeHint)
#include "test_notebooknodedelegate_sizehint.moc"
