// test_nodeinfo.cpp - Unit tests for NodeIdentifier and NodeInfo
#include <QtTest>

#include <core/nodeinfo.h>

using namespace vnotex;

namespace tests {

class TestNodeInfo : public QObject {
  Q_OBJECT

private slots:
  // NodeIdentifier tests
  void testNodeIdentifierDefault();
  void testNodeIdentifierValid();
  void testNodeIdentifierRoot();
  void testNodeIdentifierParentPath();
  void testNodeIdentifierParentPathTopLevel();
  void testNodeIdentifierParentPathRoot();
  void testNodeIdentifierEquality();
  void testNodeIdentifierHash();

  // NodeInfo tests
  void testNodeInfoDefault();
  void testNodeInfoDelegation();
  void testNodeInfoHasVisualStyleNone();
  void testNodeInfoHasVisualStyleBackground();
  void testNodeInfoHasVisualStyleBorder();
  void testNodeInfoHasVisualStyleText();
  void testNodeInfoEquality();
  void testNodeInfoHash();
};

void TestNodeInfo::testNodeIdentifierDefault() {
  NodeIdentifier id;

  QVERIFY(id.notebookId.isEmpty());
  QVERIFY(id.relativePath.isEmpty());
  QVERIFY(id.isRoot());
  QVERIFY(!id.isValid());
}

void TestNodeInfo::testNodeIdentifierValid() {
  NodeIdentifier id;
  id.notebookId = QString("nb1");
  id.relativePath = QString("folder/note.md");

  QVERIFY(id.isValid());
  QVERIFY(!id.isRoot());
}

void TestNodeInfo::testNodeIdentifierRoot() {
  NodeIdentifier id;
  id.notebookId = QString("nb1");
  id.relativePath = QString();

  QVERIFY(id.isRoot());
  QVERIFY(id.isValid());
}

void TestNodeInfo::testNodeIdentifierParentPath() {
  NodeIdentifier id;
  id.relativePath = QString("a/b/c.md");

  QCOMPARE(id.parentPath(), QString("a/b"));
}

void TestNodeInfo::testNodeIdentifierParentPathTopLevel() {
  NodeIdentifier id;
  id.relativePath = QString("note.md");

  QCOMPARE(id.parentPath(), QString());
}

void TestNodeInfo::testNodeIdentifierParentPathRoot() {
  NodeIdentifier id;
  id.relativePath = QString();

  QCOMPARE(id.parentPath(), QString());
}

void TestNodeInfo::testNodeIdentifierEquality() {
  NodeIdentifier id1;
  id1.notebookId = QString("nb1");
  id1.relativePath = QString("folder/note.md");

  NodeIdentifier id2;
  id2.notebookId = QString("nb1");
  id2.relativePath = QString("folder/note.md");

  NodeIdentifier id3;
  id3.notebookId = QString("nb1");
  id3.relativePath = QString("folder/other.md");

  QVERIFY(id1 == id2);
  QVERIFY(!(id1 != id2));
  QVERIFY(id1 != id3);
  QVERIFY(!(id1 == id3));
}

void TestNodeInfo::testNodeIdentifierHash() {
  NodeIdentifier id1;
  id1.notebookId = QString("nb1");
  id1.relativePath = QString("folder/note.md");

  NodeIdentifier id2;
  id2.notebookId = QString("nb1");
  id2.relativePath = QString("folder/note.md");

  NodeIdentifier id3;
  id3.notebookId = QString("nb1");
  id3.relativePath = QString("folder/other.md");

  QCOMPARE(qHash(id1), qHash(id2));
  QVERIFY2(qHash(id1) != qHash(id3), "Hash collision for different identifiers is unlikely");
}

void TestNodeInfo::testNodeInfoDefault() {
  NodeInfo info;

  QVERIFY(!info.id.isValid());
  QVERIFY(!info.isFolder);
  QVERIFY(!info.isExternal);
  QCOMPARE(info.childCount, 0);
  QVERIFY(info.name.isEmpty());
  QVERIFY(info.preview.isEmpty());
  QVERIFY(info.backgroundColor.isEmpty());
  QVERIFY(info.borderColor.isEmpty());
  QVERIFY(info.textColor.isEmpty());
}

void TestNodeInfo::testNodeInfoDelegation() {
  NodeInfo info;
  info.id.notebookId = QString("nb1");
  info.id.relativePath = QString("a/b/c.md");

  QCOMPARE(info.notebookId(), QString("nb1"));
  QCOMPARE(info.relativePath(), QString("a/b/c.md"));
  QVERIFY(!info.isRoot());
  QVERIFY(info.isValid());
  QCOMPARE(info.parentPath(), QString("a/b"));
}

void TestNodeInfo::testNodeInfoHasVisualStyleNone() {
  NodeInfo info;

  QVERIFY(!info.hasVisualStyle());
}

void TestNodeInfo::testNodeInfoHasVisualStyleBackground() {
  NodeInfo info;
  info.backgroundColor = QString("#112233");

  QVERIFY(info.hasVisualStyle());
}

void TestNodeInfo::testNodeInfoHasVisualStyleBorder() {
  NodeInfo info;
  info.borderColor = QString("#445566");

  QVERIFY(info.hasVisualStyle());
}

void TestNodeInfo::testNodeInfoHasVisualStyleText() {
  NodeInfo info;
  info.textColor = QString("#778899");

  QVERIFY(info.hasVisualStyle());
}

void TestNodeInfo::testNodeInfoEquality() {
  NodeInfo info1;
  info1.id.notebookId = QString("nb1");
  info1.id.relativePath = QString("folder/note.md");
  info1.name = QString("name1");

  NodeInfo info2;
  info2.id.notebookId = QString("nb1");
  info2.id.relativePath = QString("folder/note.md");
  info2.name = QString("name2");

  NodeInfo info3;
  info3.id.notebookId = QString("nb1");
  info3.id.relativePath = QString("folder/other.md");

  QVERIFY(info1 == info2);
  QVERIFY(!(info1 != info2));
  QVERIFY(info1 != info3);
  QVERIFY(!(info1 == info3));
}

void TestNodeInfo::testNodeInfoHash() {
  NodeInfo info;
  info.id.notebookId = QString("nb1");
  info.id.relativePath = QString("folder/note.md");

  QCOMPARE(qHash(info), qHash(info.id));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNodeInfo)
#include "test_nodeinfo.moc"
