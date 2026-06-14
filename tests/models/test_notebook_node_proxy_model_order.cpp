#include <QtTest>

#include <QDateTime>
#include <QVector>

#include <core/nodeidentifier.h>
#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <models/notebooknodemodel.h>
#include <models/notebooknodeproxymodel.h>

namespace tests {

// Minimal stub source model used to drive NotebookNodeProxyModel without
// pulling in the real vxcore service stack.
//
// The proxy's lessThan() and filterAcceptsRow() both call
// qobject_cast<NotebookNodeModel *>(sourceModel()); if the cast fails the
// proxy short-circuits to QSortFilterProxyModel defaults and the
// view-order switch under test is never reached. We therefore subclass
// NotebookNodeModel so the cast succeeds, then re-implement the Qt model
// interface with a flat in-memory vector of children at the root index.
// nodeIdFromIndex / nodeInfoFromIndex are also overridden so the proxy
// pulls our stub NodeInfo records instead of consulting the (empty)
// service-backed caches in the base class.
class StubNotebookNodeModel : public vnotex::NotebookNodeModel {
public:
  explicit StubNotebookNodeModel(vnotex::ServiceLocator &p_services, QObject *p_parent = nullptr)
      : vnotex::NotebookNodeModel(p_services, p_parent) {}

  void setStubChildren(const QVector<vnotex::NodeInfo> &p_children) {
    beginResetModel();
    m_children = p_children;
    endResetModel();
  }

  // --- QAbstractItemModel: flat root model ---
  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override {
    return p_parent.isValid() ? 0 : m_children.size();
  }

  int columnCount(const QModelIndex & /*p_parent*/ = QModelIndex()) const override { return 1; }

  QModelIndex index(int p_row, int p_column,
                    const QModelIndex &p_parent = QModelIndex()) const override {
    if (p_parent.isValid() || p_column != 0) {
      return QModelIndex();
    }
    if (p_row < 0 || p_row >= m_children.size()) {
      return QModelIndex();
    }
    // Use row+1 as the opaque internalId; 0 is reserved for invalid.
    return createIndex(p_row, p_column, static_cast<quintptr>(p_row + 1));
  }

  QModelIndex parent(const QModelIndex & /*p_child*/) const override { return QModelIndex(); }

  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override {
    if (!p_index.isValid() || p_index.row() < 0 || p_index.row() >= m_children.size()) {
      return QVariant();
    }
    if (p_role == Qt::DisplayRole) {
      return m_children.at(p_index.row()).name;
    }
    return QVariant();
  }

  bool hasChildren(const QModelIndex &p_parent = QModelIndex()) const override {
    return !p_parent.isValid() && !m_children.isEmpty();
  }

  bool canFetchMore(const QModelIndex & /*p_parent*/) const override { return false; }
  void fetchMore(const QModelIndex & /*p_parent*/) override {}

  // --- INodeListModel / NotebookNodeModel accessor overrides ---
  vnotex::NodeIdentifier nodeIdFromIndex(const QModelIndex &p_index) const override {
    return nodeInfoFromIndex(p_index).id;
  }

  vnotex::NodeInfo nodeInfoFromIndex(const QModelIndex &p_index) const override {
    if (!p_index.isValid() || p_index.row() < 0 || p_index.row() >= m_children.size()) {
      return vnotex::NodeInfo();
    }
    return m_children.at(p_index.row());
  }

private:
  QVector<vnotex::NodeInfo> m_children;
};

class TestNotebookNodeProxyModelOrder : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  // Test 1: the discriminating regression test. Without the proxy fix this
  // case falls through to natural-name sort and "apple.md" wins.
  void testOrderedByConfigurationPreservesSourceOrder();

  // Tests 2-5: regression coverage to confirm the other view orders are
  // untouched by the OrderedByConfiguration change.
  void testOrderedByNameSortsAlphabetically();
  void testOrderedByNameReversedSortsReverse();
  void testOrderedByCreatedTimeAscending();
  void testOrderedByModifiedTimeAscending();

  // Test 6: folder-first invariant must still rule over OrderedByConfiguration.
  void testFolderFirstInvariantUnderConfigurationOrder();

private:
  static vnotex::NodeInfo makeNode(const QString &p_name, bool p_isFolder = false,
                                   const QDateTime &p_created = QDateTime(),
                                   const QDateTime &p_modified = QDateTime());

  vnotex::ServiceLocator m_services;
  StubNotebookNodeModel *m_source = nullptr;
  vnotex::NotebookNodeProxyModel *m_proxy = nullptr;
};

vnotex::NodeInfo TestNotebookNodeProxyModelOrder::makeNode(const QString &p_name, bool p_isFolder,
                                                           const QDateTime &p_created,
                                                           const QDateTime &p_modified) {
  vnotex::NodeInfo info;
  info.name = p_name;
  info.isFolder = p_isFolder;
  info.isExternal = false;
  info.id.notebookId = QStringLiteral("test-notebook");
  info.id.relativePath = p_name;
  info.createdTimeUtc = p_created;
  info.modifiedTimeUtc = p_modified;
  return info;
}

void TestNotebookNodeProxyModelOrder::init() {
  m_source = new StubNotebookNodeModel(m_services);
  m_proxy = new vnotex::NotebookNodeProxyModel();
  m_proxy->setSourceModel(m_source);
}

void TestNotebookNodeProxyModelOrder::cleanup() {
  delete m_proxy;
  m_proxy = nullptr;
  delete m_source;
  m_source = nullptr;
}

// ===== 1. testOrderedByConfigurationPreservesSourceOrder =====

void TestNotebookNodeProxyModelOrder::testOrderedByConfigurationPreservesSourceOrder() {
  QVector<vnotex::NodeInfo> children;
  children << makeNode(QStringLiteral("zebra.md")) << makeNode(QStringLiteral("apple.md"))
           << makeNode(QStringLiteral("mango.md"));
  m_source->setStubChildren(children);

  m_proxy->setViewOrder(vnotex::ViewOrder::OrderedByConfiguration);
  m_proxy->sort(0, Qt::AscendingOrder);

  QCOMPARE(m_proxy->rowCount(), 3);
  QCOMPARE(m_proxy->index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("zebra.md"));
  QCOMPARE(m_proxy->index(1, 0).data(Qt::DisplayRole).toString(), QStringLiteral("apple.md"));
  QCOMPARE(m_proxy->index(2, 0).data(Qt::DisplayRole).toString(), QStringLiteral("mango.md"));
}

// ===== 2. testOrderedByNameSortsAlphabetically =====

void TestNotebookNodeProxyModelOrder::testOrderedByNameSortsAlphabetically() {
  QVector<vnotex::NodeInfo> children;
  children << makeNode(QStringLiteral("zebra.md")) << makeNode(QStringLiteral("apple.md"))
           << makeNode(QStringLiteral("mango.md"));
  m_source->setStubChildren(children);

  m_proxy->setViewOrder(vnotex::ViewOrder::OrderedByName);
  m_proxy->sort(0, Qt::AscendingOrder);

  QCOMPARE(m_proxy->rowCount(), 3);
  QCOMPARE(m_proxy->index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("apple.md"));
  QCOMPARE(m_proxy->index(1, 0).data(Qt::DisplayRole).toString(), QStringLiteral("mango.md"));
  QCOMPARE(m_proxy->index(2, 0).data(Qt::DisplayRole).toString(), QStringLiteral("zebra.md"));
}

// ===== 3. testOrderedByNameReversedSortsReverse =====

void TestNotebookNodeProxyModelOrder::testOrderedByNameReversedSortsReverse() {
  QVector<vnotex::NodeInfo> children;
  children << makeNode(QStringLiteral("zebra.md")) << makeNode(QStringLiteral("apple.md"))
           << makeNode(QStringLiteral("mango.md"));
  m_source->setStubChildren(children);

  m_proxy->setViewOrder(vnotex::ViewOrder::OrderedByNameReversed);
  m_proxy->sort(0, Qt::AscendingOrder);

  QCOMPARE(m_proxy->rowCount(), 3);
  QCOMPARE(m_proxy->index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("zebra.md"));
  QCOMPARE(m_proxy->index(1, 0).data(Qt::DisplayRole).toString(), QStringLiteral("mango.md"));
  QCOMPARE(m_proxy->index(2, 0).data(Qt::DisplayRole).toString(), QStringLiteral("apple.md"));
}

// ===== 4. testOrderedByCreatedTimeAscending =====

void TestNotebookNodeProxyModelOrder::testOrderedByCreatedTimeAscending() {
  const QDateTime t1 = QDateTime::fromString(QStringLiteral("2024-01-01T00:00:00Z"), Qt::ISODate);
  const QDateTime t2 = QDateTime::fromString(QStringLiteral("2024-01-02T00:00:00Z"), Qt::ISODate);
  const QDateTime t3 = QDateTime::fromString(QStringLiteral("2024-01-03T00:00:00Z"), Qt::ISODate);

  QVector<vnotex::NodeInfo> children;
  // Source row order intentionally differs from created-time order.
  children << makeNode(QStringLiteral("middle.md"), false, t2)
           << makeNode(QStringLiteral("newest.md"), false, t3)
           << makeNode(QStringLiteral("oldest.md"), false, t1);
  m_source->setStubChildren(children);

  m_proxy->setViewOrder(vnotex::ViewOrder::OrderedByCreatedTime);
  m_proxy->sort(0, Qt::AscendingOrder);

  QCOMPARE(m_proxy->rowCount(), 3);
  QCOMPARE(m_proxy->index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("oldest.md"));
  QCOMPARE(m_proxy->index(1, 0).data(Qt::DisplayRole).toString(), QStringLiteral("middle.md"));
  QCOMPARE(m_proxy->index(2, 0).data(Qt::DisplayRole).toString(), QStringLiteral("newest.md"));
}

// ===== 5. testOrderedByModifiedTimeAscending =====

void TestNotebookNodeProxyModelOrder::testOrderedByModifiedTimeAscending() {
  const QDateTime t1 = QDateTime::fromString(QStringLiteral("2024-06-01T00:00:00Z"), Qt::ISODate);
  const QDateTime t2 = QDateTime::fromString(QStringLiteral("2024-06-02T00:00:00Z"), Qt::ISODate);
  const QDateTime t3 = QDateTime::fromString(QStringLiteral("2024-06-03T00:00:00Z"), Qt::ISODate);

  QVector<vnotex::NodeInfo> children;
  // Source row order intentionally differs from modified-time order.
  children << makeNode(QStringLiteral("recent.md"), false, QDateTime(), t3)
           << makeNode(QStringLiteral("stale.md"), false, QDateTime(), t1)
           << makeNode(QStringLiteral("midway.md"), false, QDateTime(), t2);
  m_source->setStubChildren(children);

  m_proxy->setViewOrder(vnotex::ViewOrder::OrderedByModifiedTime);
  m_proxy->sort(0, Qt::AscendingOrder);

  QCOMPARE(m_proxy->rowCount(), 3);
  QCOMPARE(m_proxy->index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("stale.md"));
  QCOMPARE(m_proxy->index(1, 0).data(Qt::DisplayRole).toString(), QStringLiteral("midway.md"));
  QCOMPARE(m_proxy->index(2, 0).data(Qt::DisplayRole).toString(), QStringLiteral("recent.md"));
}

// ===== 6. testFolderFirstInvariantUnderConfigurationOrder =====

void TestNotebookNodeProxyModelOrder::testFolderFirstInvariantUnderConfigurationOrder() {
  QVector<vnotex::NodeInfo> children;
  // Plan-mandated source: folder then file. Folder-first must hold,
  // i.e. the folder ends up at proxy index 0 even though lexicographically
  // "a_file.md" sorts ahead of "b_folder".
  children << makeNode(QStringLiteral("b_folder"), /*p_isFolder=*/true)
           << makeNode(QStringLiteral("a_file.md"), /*p_isFolder=*/false);
  m_source->setStubChildren(children);

  m_proxy->setViewOrder(vnotex::ViewOrder::OrderedByConfiguration);
  m_proxy->sort(0, Qt::AscendingOrder);

  QCOMPARE(m_proxy->rowCount(), 2);
  QCOMPARE(m_proxy->index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("b_folder"));
  QCOMPARE(m_proxy->index(1, 0).data(Qt::DisplayRole).toString(), QStringLiteral("a_file.md"));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookNodeProxyModelOrder)
#include "test_notebook_node_proxy_model_order.moc"
