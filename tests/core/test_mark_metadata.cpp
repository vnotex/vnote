#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <core/nodeinfo.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestMarkMetadata : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testMetadataMergePreservesExistingKeys();
  void testSetAndClearColors();
  void testNodeInfoVisualStyleMetadataRoundTrip();
  void testClearRemovesKeyFromMetadata();
  void testRawNotebookMetadataUnsupported();

private:
  QString createBundledNotebook(const QString &p_path);
  QString createRawNotebook(const QString &p_path);

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_service = nullptr;
  HookManager *m_hookMgr = nullptr;
  TempDirFixture m_tempDir;
};

void TestMarkMetadata::initTestCase() {
  QVERIFY(m_tempDir.isValid());
  vxcore_set_test_mode(1);
  QString configJson = "{}";
  VxCoreError err = vxcore_context_create(configJson.toUtf8().constData(), &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);
  m_service = new NotebookCoreService(m_context, this);
  QVERIFY(m_service != nullptr);
  m_hookMgr = new HookManager(this);
  m_service->setHookManager(m_hookMgr);
}

void TestMarkMetadata::cleanupTestCase() {
  delete m_hookMgr;
  m_hookMgr = nullptr;

  delete m_service;
  m_service = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestMarkMetadata::cleanup() {
  QJsonArray notebooks = m_service->listNotebooks();
  for (const auto &val : notebooks) {
    QString id = val.toObject()["id"].toString();
    if (!id.isEmpty()) {
      m_service->closeNotebook(id);
    }
  }
}

QString TestMarkMetadata::createBundledNotebook(const QString &p_path) {
  QString configJson = R"({"name": "Test", "description": "test", "version": "1"})";
  return m_service->createNotebook(p_path, configJson, NotebookType::Bundled);
}

QString TestMarkMetadata::createRawNotebook(const QString &p_path) {
  QString configJson = R"({"name": "RawTest", "description": "test", "version": "1"})";
  return m_service->createNotebook(p_path, configJson, NotebookType::Raw);
}

void TestMarkMetadata::testMetadataMergePreservesExistingKeys() {
  QString nbPath = m_tempDir.filePath("merge_nb");
  QString nbId = createBundledNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QString fileId = m_service->createFile(nbId, "", "test.md");
  QVERIFY(!fileId.isEmpty());

  QString initialMeta = R"({"customKey": "value", "anotherKey": 42})";
  bool ok = m_service->updateFileMetadata(nbId, "test.md", initialMeta);
  QVERIFY(ok);

  QJsonObject metadata = m_service->getFileMetadata(nbId, "test.md");
  QVERIFY(metadata.contains("customKey"));

  metadata["textColor"] = "#e53935";
  metadata.remove("backgroundColor");

  QJsonDocument doc(metadata);
  ok = m_service->updateFileMetadata(nbId, "test.md",
                                     QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
  QVERIFY(ok);

  QJsonObject result = m_service->getFileMetadata(nbId, "test.md");
  QCOMPARE(result.value("customKey").toString(), QString("value"));
  QCOMPARE(result.value("anotherKey").toInt(), 42);
  QCOMPARE(result.value("textColor").toString(), QString("#e53935"));
  QVERIFY(!result.contains("backgroundColor"));
}

void TestMarkMetadata::testSetAndClearColors() {
  QString nbPath = m_tempDir.filePath("setclear_nb");
  QString nbId = createBundledNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QString fileId = m_service->createFile(nbId, "", "note.md");
  QVERIFY(!fileId.isEmpty());

  QJsonObject meta;
  meta["textColor"] = "#1e88e5";
  meta["backgroundColor"] = "#43a047";
  QJsonDocument doc(meta);
  bool ok = m_service->updateFileMetadata(nbId, "note.md",
                                          QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
  QVERIFY(ok);

  QJsonObject result = m_service->getFileMetadata(nbId, "note.md");
  QCOMPARE(result.value("textColor").toString(), QString("#1e88e5"));
  QCOMPARE(result.value("backgroundColor").toString(), QString("#43a047"));

  result.remove("textColor");
  result.remove("backgroundColor");
  QJsonDocument doc2(result);
  ok = m_service->updateFileMetadata(nbId, "note.md",
                                     QString::fromUtf8(doc2.toJson(QJsonDocument::Compact)));
  QVERIFY(ok);

  QJsonObject result2 = m_service->getFileMetadata(nbId, "note.md");
  QVERIFY(!result2.contains("textColor"));
  QVERIFY(!result2.contains("backgroundColor"));
}

void TestMarkMetadata::testNodeInfoVisualStyleMetadataRoundTrip() {
  QString nbPath = m_tempDir.filePath("nodeinfo_nb");
  QString nbId = createBundledNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QString fileId = m_service->createFile(nbId, "", "nodeinfo.md");
  QVERIFY(!fileId.isEmpty());

  QJsonObject meta;
  meta["textColor"] = "#263238";
  meta["backgroundColor"] = "#e0f7fa";
  QJsonDocument doc(meta);
  bool ok = m_service->updateFileMetadata(nbId, "nodeinfo.md",
                                          QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
  QVERIFY(ok);

  QJsonObject roundTripped = m_service->getFileMetadata(nbId, "nodeinfo.md");
  NodeInfo styled;
  styled.textColor = roundTripped.value("textColor").toString();
  styled.backgroundColor = roundTripped.value("backgroundColor").toString();
  QVERIFY(styled.hasVisualStyle());

  roundTripped.remove("textColor");
  roundTripped.remove("backgroundColor");
  QJsonDocument doc2(roundTripped);
  ok = m_service->updateFileMetadata(nbId, "nodeinfo.md",
                                     QString::fromUtf8(doc2.toJson(QJsonDocument::Compact)));
  QVERIFY(ok);

  QJsonObject cleared = m_service->getFileMetadata(nbId, "nodeinfo.md");
  NodeInfo plain;
  plain.textColor = cleared.value("textColor").toString();
  plain.backgroundColor = cleared.value("backgroundColor").toString();
  QVERIFY(!plain.hasVisualStyle());
}

void TestMarkMetadata::testClearRemovesKeyFromMetadata() {
  QString nbPath = m_tempDir.filePath("clear_nb");
  QString nbId = createBundledNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QString folderId = m_service->createFolder(nbId, "", "folder1");
  QVERIFY(!folderId.isEmpty());

  QJsonObject meta;
  meta["textColor"] = "#ff0000";
  meta["preserve"] = "keep";
  QJsonDocument doc(meta);
  bool ok = m_service->updateFolderMetadata(nbId, "folder1",
                                            QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
  QVERIFY(ok);

  QJsonObject read = m_service->getFolderMetadata(nbId, "folder1");
  read.remove("textColor");
  QJsonDocument doc2(read);
  ok = m_service->updateFolderMetadata(nbId, "folder1",
                                       QString::fromUtf8(doc2.toJson(QJsonDocument::Compact)));
  QVERIFY(ok);

  QJsonObject result = m_service->getFolderMetadata(nbId, "folder1");
  QVERIFY(!result.contains("textColor"));
  QCOMPARE(result.value("preserve").toString(), QString("keep"));
}

void TestMarkMetadata::testRawNotebookMetadataUnsupported() {
  QString nbPath = m_tempDir.filePath("raw_nb");
  QString nbId = createRawNotebook(nbPath);
  QVERIFY(!nbId.isEmpty());

  QJsonObject meta = m_service->getFileMetadata(nbId, "anything.md");
  QVERIFY(meta.isEmpty());

  bool ok = m_service->updateFileMetadata(nbId, "anything.md", R"({"textColor": "#fff"})");
  QVERIFY(!ok);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestMarkMetadata)
#include "test_mark_metadata.moc"
