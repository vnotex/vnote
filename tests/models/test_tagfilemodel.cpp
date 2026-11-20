#include <QtTest>

#include <core/nodeidentifier.h>
#include <core/nodeinfo.h>
#include <models/inodelistmodel.h>
#include <models/tagfilemodel.h>

namespace tests {

class TestTagFileModel : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  // Basic construction
  void testEmptyModel();

  // setNodes and rowCount
  void testSetNodesBasic();
  void testNodeInfoFields();

  // Data roles
  void testNodeIdentifierRole();
  void testIsFolderRole();
  void testPathRole();
  void testPreviewRole();
  void testDisplayRole();
  void testToolTipRole();

  // setNodes behaviour
  void testSetNodesReset();
  void testSetNodesEmpty();

  // Fallback behaviour
  void testFallbackFileName();
  void testFallbackNotebookId();

  // INodeListModel interface
  void testNodeIdFromIndex();
  void testNodeInfoFromIndex();
  void testIndexFromNodeId();
  void testIndexFromNodeIdNotFound();

  // Capabilities
  void testCapabilities();

private:
  vnotex::TagFileModel *m_model = nullptr;

  // Helpers build owning tag matches without result-envelope JSON.
  static vnotex::TagFileMatch makeNode(const QString &p_filePath,
                                       const QString &p_fileName = QString(),
                                       const QStringList &p_tags = QStringList(),
                                       const QString &p_notebookId = QString());

  static QVector<vnotex::TagFileMatch> makeThreeNodeArray();
};

// ===== Helpers =====

vnotex::TagFileMatch TestTagFileModel::makeNode(const QString &p_filePath,
                                                const QString &p_fileName,
                                                const QStringList &p_tags,
                                                const QString &p_notebookId) {
  return {p_filePath, p_fileName, p_tags, p_notebookId};
}

QVector<vnotex::TagFileMatch> TestTagFileModel::makeThreeNodeArray() {
  QVector<vnotex::TagFileMatch> arr;
  QStringList tags1;
  tags1 << QStringLiteral("tag1") << QStringLiteral("tag2");
  arr.append(makeNode(QStringLiteral("notes/hello.md"), QStringLiteral("hello.md"), tags1));
  QStringList tags2;
  tags2 << QStringLiteral("docs");
  arr.append(makeNode(QStringLiteral("docs/readme.md"), QStringLiteral("readme.md"), tags2));
  arr.append(makeNode(QStringLiteral("archive/old.txt"), QStringLiteral("old.txt")));
  return arr;
}

// ===== Setup / Teardown =====

void TestTagFileModel::initTestCase() { m_model = new vnotex::TagFileModel(nullptr, this); }

void TestTagFileModel::cleanupTestCase() {
  delete m_model;
  m_model = nullptr;
}

// ===== 1. testEmptyModel =====

void TestTagFileModel::testEmptyModel() {
  // A fresh model (no setNodes called) must have zero rows.
  vnotex::TagFileModel emptyModel(nullptr);
  QCOMPARE(emptyModel.rowCount(), 0);

  // data() on an invalid index returns invalid QVariant.
  QVariant val = emptyModel.data(emptyModel.index(0, 0), Qt::DisplayRole);
  QVERIFY(!val.isValid());
}

// ===== 2. testSetNodesBasic =====

void TestTagFileModel::testSetNodesBasic() {
  const auto arr = makeThreeNodeArray();
  m_model->setNodes(arr, QStringLiteral("nb-fallback"));

  QCOMPARE(m_model->rowCount(), 3);

  // Verify each row returns a valid NodeInfo via NodeInfoRole.
  for (int i = 0; i < 3; ++i) {
    QVariant v = m_model->data(m_model->index(i, 0), vnotex::INodeListModel::NodeInfoRole);
    QVERIFY(v.isValid());
    auto info = v.value<vnotex::NodeInfo>();
    QVERIFY(info.isValid());
  }
}

// ===== 3. testNodeInfoFields =====

void TestTagFileModel::testNodeInfoFields() {
  QVector<vnotex::TagFileMatch> arr;
  QStringList tags;
  tags << QStringLiteral("tag1") << QStringLiteral("tag2");
  arr.append(makeNode(QStringLiteral("notes/hello.md"), QStringLiteral("hello.md"), tags,
                      QStringLiteral("nb-123")));
  m_model->setNodes(arr, QStringLiteral("nb-fallback"));

  QVariant v = m_model->data(m_model->index(0, 0), vnotex::INodeListModel::NodeInfoRole);
  auto info = v.value<vnotex::NodeInfo>();

  // name == fileName from the match.
  QCOMPARE(info.name, QStringLiteral("hello.md"));

  // id.notebookId == per-item notebookId (not fallback).
  QCOMPARE(info.id.notebookId, QStringLiteral("nb-123"));

  // id.relativePath == filePath from the match.
  QCOMPARE(info.id.relativePath, QStringLiteral("notes/hello.md"));

  // isFolder is always false for tag file results
  QCOMPARE(info.isFolder, false);

  // Preserve the complete tag list.
  QStringList expectedTags;
  expectedTags << QStringLiteral("tag1") << QStringLiteral("tag2");
  QCOMPARE(info.tags, expectedTags);
}

// ===== 4. testNodeIdentifierRole =====

void TestTagFileModel::testNodeIdentifierRole() {
  QVector<vnotex::TagFileMatch> arr;
  arr.append(makeNode(QStringLiteral("docs/guide.md"), QStringLiteral("guide.md"), QStringList(),
                      QStringLiteral("nb-abc")));
  m_model->setNodes(arr, QStringLiteral("nb-fallback"));

  QVariant v = m_model->data(m_model->index(0, 0), vnotex::INodeListModel::NodeIdentifierRole);
  QVERIFY(v.isValid());
  auto id = v.value<vnotex::NodeIdentifier>();
  QCOMPARE(id.notebookId, QStringLiteral("nb-abc"));
  QCOMPARE(id.relativePath, QStringLiteral("docs/guide.md"));
}

// ===== 5. testIsFolderRole =====

void TestTagFileModel::testIsFolderRole() {
  m_model->setNodes(makeThreeNodeArray(), QStringLiteral("nb-1"));

  for (int i = 0; i < m_model->rowCount(); ++i) {
    QVariant v = m_model->data(m_model->index(i, 0), vnotex::INodeListModel::IsFolderRole);
    QCOMPARE(v.toBool(), false);
  }
}

// ===== 6. testPathRole =====

void TestTagFileModel::testPathRole() {
  QVector<vnotex::TagFileMatch> arr;
  arr.append(makeNode(QStringLiteral("folder/sub/file.md"), QStringLiteral("file.md")));
  m_model->setNodes(arr, QStringLiteral("nb-1"));

  QVariant v = m_model->data(m_model->index(0, 0), vnotex::INodeListModel::PathRole);
  QCOMPARE(v.toString(), QStringLiteral("folder/sub/file.md"));
}

// ===== 7. testPreviewRole =====

void TestTagFileModel::testPreviewRole() {
  QVector<vnotex::TagFileMatch> arr;
  arr.append(makeNode(QStringLiteral("a.md"), QStringLiteral("a.md")));
  m_model->setNodes(arr, QStringLiteral("nb-1"));

  QVariant v = m_model->data(m_model->index(0, 0), vnotex::INodeListModel::PreviewRole);
  QCOMPARE(v.toString(), QString());
}

// ===== 8. testDisplayRole =====

void TestTagFileModel::testDisplayRole() {
  QVector<vnotex::TagFileMatch> arr;
  arr.append(makeNode(QStringLiteral("notes/hello.md"), QStringLiteral("hello.md")));
  m_model->setNodes(arr, QStringLiteral("nb-1"));

  QVariant v = m_model->data(m_model->index(0, 0), Qt::DisplayRole);
  QCOMPARE(v.toString(), QStringLiteral("hello.md"));
}

// ===== 9. testToolTipRole =====

void TestTagFileModel::testToolTipRole() {
  QVector<vnotex::TagFileMatch> arr;
  arr.append(makeNode(QStringLiteral("deep/path/note.md"), QStringLiteral("note.md")));
  m_model->setNodes(arr, QStringLiteral("nb-1"));

  QVariant v = m_model->data(m_model->index(0, 0), Qt::ToolTipRole);
  QCOMPARE(v.toString(), QStringLiteral("deep/path/note.md"));
}

// ===== 10. testSetNodesReset =====

void TestTagFileModel::testSetNodesReset() {
  // First call with 3 items.
  m_model->setNodes(makeThreeNodeArray(), QStringLiteral("nb-1"));
  QCOMPARE(m_model->rowCount(), 3);

  // Second call with 1 item - model must reflect the new data.
  QVector<vnotex::TagFileMatch> arr;
  QStringList tags;
  tags << QStringLiteral("only");
  arr.append(makeNode(QStringLiteral("single.md"), QStringLiteral("single.md"), tags));
  m_model->setNodes(arr, QStringLiteral("nb-2"));
  QCOMPARE(m_model->rowCount(), 1);

  QVariant v = m_model->data(m_model->index(0, 0), Qt::DisplayRole);
  QCOMPARE(v.toString(), QStringLiteral("single.md"));
}

// ===== 11. testSetNodesEmpty =====

void TestTagFileModel::testSetNodesEmpty() {
  // Populate first, then clear.
  m_model->setNodes(makeThreeNodeArray(), QStringLiteral("nb-1"));
  QCOMPARE(m_model->rowCount(), 3);

  m_model->setNodes({}, QStringLiteral("nb-1"));
  QCOMPARE(m_model->rowCount(), 0);
}

// ===== 12. testFallbackFileName =====

void TestTagFileModel::testFallbackFileName() {
  // Empty fileName falls back to the last component of filePath.
  QVector<vnotex::TagFileMatch> arr;
  arr.append(makeNode(QStringLiteral("folder/deep/myfile.md")));
  m_model->setNodes(arr, QStringLiteral("nb-1"));

  QVariant v = m_model->data(m_model->index(0, 0), Qt::DisplayRole);
  QCOMPARE(v.toString(), QStringLiteral("myfile.md"));
}

// ===== 13. testFallbackNotebookId =====

void TestTagFileModel::testFallbackNotebookId() {
  const auto absent = makeNode(QStringLiteral("note.md"), QStringLiteral("note.md"));
  auto explicitEmpty = absent;
  explicitEmpty.m_notebookId = QStringLiteral("");
  auto explicitNotebook = absent;
  explicitNotebook.m_notebookId = QStringLiteral("nb-explicit");
  QVERIFY(absent.m_notebookId.isNull());
  QVERIFY(!explicitEmpty.m_notebookId.isNull());
  QVERIFY(absent != explicitEmpty);
  QVERIFY((QVector<vnotex::TagFileMatch>{absent} != QVector<vnotex::TagFileMatch>{explicitEmpty}));

  m_model->setNodes({absent, explicitEmpty, explicitNotebook}, QStringLiteral("nb-fallback-id"));
  const auto fallbackId = m_model->nodeIdFromIndex(m_model->index(0, 0));
  QCOMPARE(fallbackId.notebookId, QStringLiteral("nb-fallback-id"));
  const auto emptyId = m_model->nodeIdFromIndex(m_model->index(1, 0));
  QVERIFY(emptyId.notebookId.isEmpty());
  QVERIFY(!emptyId.notebookId.isNull());
  QCOMPARE(m_model->nodeIdFromIndex(m_model->index(2, 0)).notebookId,
           QStringLiteral("nb-explicit"));
  QCOMPARE(m_model->getNotebookId(), QStringLiteral("nb-fallback-id"));
}

// ===== 14. testNodeIdFromIndex =====

void TestTagFileModel::testNodeIdFromIndex() {
  QVector<vnotex::TagFileMatch> arr;
  arr.append(makeNode(QStringLiteral("a/b.md"), QStringLiteral("b.md"), QStringList(),
                      QStringLiteral("nb-x")));
  arr.append(makeNode(QStringLiteral("c/d.md"), QStringLiteral("d.md"), QStringList(),
                      QStringLiteral("nb-y")));
  m_model->setNodes(arr, QStringLiteral("nb-fallback"));

  vnotex::NodeIdentifier id0 = m_model->nodeIdFromIndex(m_model->index(0, 0));
  QCOMPARE(id0.notebookId, QStringLiteral("nb-x"));
  QCOMPARE(id0.relativePath, QStringLiteral("a/b.md"));

  vnotex::NodeIdentifier id1 = m_model->nodeIdFromIndex(m_model->index(1, 0));
  QCOMPARE(id1.notebookId, QStringLiteral("nb-y"));
  QCOMPARE(id1.relativePath, QStringLiteral("c/d.md"));

  // Invalid index returns invalid NodeIdentifier.
  vnotex::NodeIdentifier idBad = m_model->nodeIdFromIndex(QModelIndex());
  QVERIFY(!idBad.isValid());
}

// ===== 15. testNodeInfoFromIndex =====

void TestTagFileModel::testNodeInfoFromIndex() {
  QVector<vnotex::TagFileMatch> arr;
  QStringList tags;
  tags << QStringLiteral("t1");
  arr.append(
      makeNode(QStringLiteral("x/y.md"), QStringLiteral("y.md"), tags, QStringLiteral("nb-z")));
  m_model->setNodes(arr, QStringLiteral("nb-fallback"));

  vnotex::NodeInfo info = m_model->nodeInfoFromIndex(m_model->index(0, 0));
  QVERIFY(info.isValid());
  QCOMPARE(info.name, QStringLiteral("y.md"));
  QCOMPARE(info.id.notebookId, QStringLiteral("nb-z"));
  QCOMPARE(info.id.relativePath, QStringLiteral("x/y.md"));
  QStringList expectedTags;
  expectedTags << QStringLiteral("t1");
  QCOMPARE(info.tags, expectedTags);

  // Invalid index returns invalid NodeInfo.
  vnotex::NodeInfo badInfo = m_model->nodeInfoFromIndex(QModelIndex());
  QVERIFY(!badInfo.isValid());
}

// ===== 16. testIndexFromNodeId =====

void TestTagFileModel::testIndexFromNodeId() {
  QVector<vnotex::TagFileMatch> arr;
  arr.append(makeNode(QStringLiteral("first.md"), QStringLiteral("first.md"), QStringList(),
                      QStringLiteral("nb-1")));
  arr.append(makeNode(QStringLiteral("second.md"), QStringLiteral("second.md"), QStringList(),
                      QStringLiteral("nb-1")));
  arr.append(makeNode(QStringLiteral("third.md"), QStringLiteral("third.md"), QStringList(),
                      QStringLiteral("nb-1")));
  m_model->setNodes(arr, QStringLiteral("nb-1"));

  vnotex::NodeIdentifier target;
  target.notebookId = QStringLiteral("nb-1");
  target.relativePath = QStringLiteral("second.md");

  QModelIndex idx = m_model->indexFromNodeId(target);
  QVERIFY(idx.isValid());
  QCOMPARE(idx.row(), 1);
}

// ===== 17. testIndexFromNodeIdNotFound =====

void TestTagFileModel::testIndexFromNodeIdNotFound() {
  QVector<vnotex::TagFileMatch> arr;
  arr.append(makeNode(QStringLiteral("exists.md"), QStringLiteral("exists.md"), QStringList(),
                      QStringLiteral("nb-1")));
  m_model->setNodes(arr, QStringLiteral("nb-1"));

  vnotex::NodeIdentifier unknown;
  unknown.notebookId = QStringLiteral("nb-999");
  unknown.relativePath = QStringLiteral("nonexistent.md");

  QModelIndex idx = m_model->indexFromNodeId(unknown);
  QVERIFY(!idx.isValid());
}

// ===== 18. testCapabilities =====

void TestTagFileModel::testCapabilities() {
  QCOMPARE(m_model->supportsDragDrop(), false);
  QCOMPARE(m_model->supportsPreview(), true);
  QCOMPARE(m_model->supportsHierarchy(), false);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestTagFileModel)
#include "test_tagfilemodel.moc"
