#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

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

  // Helper to build a JSON node object.
  static QJsonObject makeNode(const QString &p_filePath,
                              const QString &p_fileName = QString(),
                              const QStringList &p_tags = QStringList(),
                              const QString &p_notebookId = QString());

  // Helper to build a 3-element test array.
  static QJsonArray makeThreeNodeArray();
};

// ===== Helpers =====

QJsonObject TestTagFileModel::makeNode(const QString &p_filePath,
                                       const QString &p_fileName,
                                       const QStringList &p_tags,
                                       const QString &p_notebookId) {
  QJsonObject obj;
  obj.insert(QStringLiteral("filePath"), p_filePath);
  if (!p_fileName.isEmpty()) {
    obj.insert(QStringLiteral("fileName"), p_fileName);
  }
  if (!p_tags.isEmpty()) {
    QJsonArray tagsArr;
    for (const auto &t : p_tags) {
      tagsArr.append(QJsonValue(t));
    }
    obj.insert(QStringLiteral("tags"), tagsArr);
  }
  if (!p_notebookId.isEmpty()) {
    obj.insert(QStringLiteral("notebookId"), p_notebookId);
  }
  return obj;
}

QJsonArray TestTagFileModel::makeThreeNodeArray() {
  QJsonArray arr;
  QStringList tags1;
  tags1 << QStringLiteral("tag1") << QStringLiteral("tag2");
  arr.append(QJsonValue(makeNode(QStringLiteral("notes/hello.md"),
                                 QStringLiteral("hello.md"),
                                 tags1)));
  QStringList tags2;
  tags2 << QStringLiteral("docs");
  arr.append(QJsonValue(makeNode(QStringLiteral("docs/readme.md"),
                                 QStringLiteral("readme.md"),
                                 tags2)));
  arr.append(QJsonValue(makeNode(QStringLiteral("archive/old.txt"),
                                 QStringLiteral("old.txt"))));
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
  QJsonArray arr = makeThreeNodeArray();
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
  QJsonArray arr;
  QStringList tags;
  tags << QStringLiteral("tag1") << QStringLiteral("tag2");
  arr.append(QJsonValue(makeNode(QStringLiteral("notes/hello.md"),
                                 QStringLiteral("hello.md"),
                                 tags,
                                 QStringLiteral("nb-123"))));
  m_model->setNodes(arr, QStringLiteral("nb-fallback"));

  QVariant v = m_model->data(m_model->index(0, 0), vnotex::INodeListModel::NodeInfoRole);
  auto info = v.value<vnotex::NodeInfo>();

  // name == fileName from JSON
  QCOMPARE(info.name, QStringLiteral("hello.md"));

  // id.notebookId == per-object notebookId (not fallback)
  QCOMPARE(info.id.notebookId, QStringLiteral("nb-123"));

  // id.relativePath == filePath from JSON
  QCOMPARE(info.id.relativePath, QStringLiteral("notes/hello.md"));

  // isFolder is always false for tag file results
  QCOMPARE(info.isFolder, false);

  // tags match JSON array
  QStringList expectedTags;
  expectedTags << QStringLiteral("tag1") << QStringLiteral("tag2");
  QCOMPARE(info.tags, expectedTags);
}

// ===== 4. testNodeIdentifierRole =====

void TestTagFileModel::testNodeIdentifierRole() {
  QJsonArray arr;
  arr.append(QJsonValue(makeNode(QStringLiteral("docs/guide.md"),
                                 QStringLiteral("guide.md"),
                                 QStringList(),
                                 QStringLiteral("nb-abc"))));
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
  QJsonArray arr;
  arr.append(QJsonValue(makeNode(QStringLiteral("folder/sub/file.md"),
                                 QStringLiteral("file.md"))));
  m_model->setNodes(arr, QStringLiteral("nb-1"));

  QVariant v = m_model->data(m_model->index(0, 0), vnotex::INodeListModel::PathRole);
  QCOMPARE(v.toString(), QStringLiteral("folder/sub/file.md"));
}

// ===== 7. testPreviewRole =====

void TestTagFileModel::testPreviewRole() {
  QJsonArray arr;
  arr.append(QJsonValue(makeNode(QStringLiteral("a.md"), QStringLiteral("a.md"))));
  m_model->setNodes(arr, QStringLiteral("nb-1"));

  QVariant v = m_model->data(m_model->index(0, 0), vnotex::INodeListModel::PreviewRole);
  QCOMPARE(v.toString(), QString());
}

// ===== 8. testDisplayRole =====

void TestTagFileModel::testDisplayRole() {
  QJsonArray arr;
  arr.append(QJsonValue(makeNode(QStringLiteral("notes/hello.md"),
                                 QStringLiteral("hello.md"))));
  m_model->setNodes(arr, QStringLiteral("nb-1"));

  QVariant v = m_model->data(m_model->index(0, 0), Qt::DisplayRole);
  QCOMPARE(v.toString(), QStringLiteral("hello.md"));
}

// ===== 9. testToolTipRole =====

void TestTagFileModel::testToolTipRole() {
  QJsonArray arr;
  arr.append(QJsonValue(makeNode(QStringLiteral("deep/path/note.md"),
                                 QStringLiteral("note.md"))));
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
  QJsonArray arr;
  QStringList tags;
  tags << QStringLiteral("only");
  arr.append(QJsonValue(makeNode(QStringLiteral("single.md"),
                                 QStringLiteral("single.md"),
                                 tags)));
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

  m_model->setNodes(QJsonArray(), QStringLiteral("nb-1"));
  QCOMPARE(m_model->rowCount(), 0);
}

// ===== 12. testFallbackFileName =====

void TestTagFileModel::testFallbackFileName() {
  // No "fileName" key - name should be last component of filePath.
  QJsonArray arr;
  arr.append(QJsonValue(makeNode(QStringLiteral("folder/deep/myfile.md")))); // no fileName
  m_model->setNodes(arr, QStringLiteral("nb-1"));

  QVariant v = m_model->data(m_model->index(0, 0), Qt::DisplayRole);
  QCOMPARE(v.toString(), QStringLiteral("myfile.md"));
}

// ===== 13. testFallbackNotebookId =====

void TestTagFileModel::testFallbackNotebookId() {
  // No "notebookId" key in the JSON object - should use the fallback parameter.
  QJsonArray arr;
  arr.append(QJsonValue(makeNode(QStringLiteral("note.md"),
                                 QStringLiteral("note.md")))); // no notebookId in object
  m_model->setNodes(arr, QStringLiteral("nb-fallback-id"));

  QVariant v = m_model->data(m_model->index(0, 0), vnotex::INodeListModel::NodeIdentifierRole);
  auto id = v.value<vnotex::NodeIdentifier>();
  QCOMPARE(id.notebookId, QStringLiteral("nb-fallback-id"));
}

// ===== 14. testNodeIdFromIndex =====

void TestTagFileModel::testNodeIdFromIndex() {
  QJsonArray arr;
  arr.append(QJsonValue(makeNode(QStringLiteral("a/b.md"),
                                 QStringLiteral("b.md"),
                                 QStringList(),
                                 QStringLiteral("nb-x"))));
  arr.append(QJsonValue(makeNode(QStringLiteral("c/d.md"),
                                 QStringLiteral("d.md"),
                                 QStringList(),
                                 QStringLiteral("nb-y"))));
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
  QJsonArray arr;
  QStringList tags;
  tags << QStringLiteral("t1");
  arr.append(QJsonValue(makeNode(QStringLiteral("x/y.md"),
                                 QStringLiteral("y.md"),
                                 tags,
                                 QStringLiteral("nb-z"))));
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
  QJsonArray arr;
  arr.append(QJsonValue(makeNode(QStringLiteral("first.md"),
                                 QStringLiteral("first.md"),
                                 QStringList(),
                                 QStringLiteral("nb-1"))));
  arr.append(QJsonValue(makeNode(QStringLiteral("second.md"),
                                 QStringLiteral("second.md"),
                                 QStringList(),
                                 QStringLiteral("nb-1"))));
  arr.append(QJsonValue(makeNode(QStringLiteral("third.md"),
                                 QStringLiteral("third.md"),
                                 QStringList(),
                                 QStringLiteral("nb-1"))));
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
  QJsonArray arr;
  arr.append(QJsonValue(makeNode(QStringLiteral("exists.md"),
                                 QStringLiteral("exists.md"),
                                 QStringList(),
                                 QStringLiteral("nb-1"))));
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
