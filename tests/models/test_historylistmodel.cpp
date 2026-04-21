#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include <core/nodeidentifier.h>
#include <core/nodeinfo.h>
#include <core/services/buffercoreservice.h>
#include <core/services/notebookcoreservice.h>
#include <models/historylistmodel.h>
#include <models/inodelistmodel.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestHistoryListModel : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void test_emptyHistory();
  void test_singleNotebookHistory();
  void test_multiNotebookMerge();
  void test_nodeIdFromIndex();
  void test_indexFromNodeId();
  void test_dataRoles();

private:
  QString createTestNotebook(const QString &p_name);

  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  BufferCoreService *m_bufferService = nullptr;
  TempDirFixture m_tempDir;
};

void TestHistoryListModel::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new NotebookCoreService(m_context, this);
  m_bufferService = new BufferCoreService(m_context, this);
}

void TestHistoryListModel::cleanupTestCase() {
  delete m_bufferService;
  m_bufferService = nullptr;

  delete m_notebookService;
  m_notebookService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestHistoryListModel::cleanup() {
  // Close all buffers.
  QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &bufVal : buffers) {
    QString id = bufVal.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_bufferService->closeBuffer(id);
    }
  }

  // Close all notebooks.
  QJsonArray notebooks = m_notebookService->listNotebooks();
  for (const auto &nbVal : notebooks) {
    QString id = nbVal.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_notebookService->closeNotebook(id);
    }
  }
}

QString TestHistoryListModel::createTestNotebook(const QString &p_name) {
  QString nbPath = m_tempDir.filePath(p_name);
  QString configJson =
      QStringLiteral(R"({"name": "%1", "description": "Test", "version": "1"})").arg(p_name);
  return m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
}

void TestHistoryListModel::test_emptyHistory() {
  QString nbId = createTestNotebook(QStringLiteral("empty_nb"));
  QVERIFY(!nbId.isEmpty());

  HistoryListModel model(m_notebookService);
  model.loadHistory();
  QCOMPARE(model.rowCount(), 0);
}

void TestHistoryListModel::test_singleNotebookHistory() {
  QString nbId = createTestNotebook(QStringLiteral("single_nb"));
  QVERIFY(!nbId.isEmpty());

  // Create 2 files.
  QString fileId1 =
      m_notebookService->createFile(nbId, QString(), QStringLiteral("note1.md"));
  QVERIFY(!fileId1.isEmpty());
  QString fileId2 =
      m_notebookService->createFile(nbId, QString(), QStringLiteral("note2.md"));
  QVERIFY(!fileId2.isEmpty());

  // Open buffers to record history.
  QString buf1 = m_bufferService->openBuffer(nbId, QStringLiteral("note1.md"));
  QVERIFY(!buf1.isEmpty());
  QString buf2 = m_bufferService->openBuffer(nbId, QStringLiteral("note2.md"));
  QVERIFY(!buf2.isEmpty());

  HistoryListModel model(m_notebookService);
  model.loadHistory();
  QCOMPARE(model.rowCount(), 2);

  // Verify first item has correct notebookId.
  QVariant v = model.data(model.index(0, 0), INodeListModel::NodeInfoRole);
  QVERIFY(v.isValid());
  NodeInfo info = v.value<NodeInfo>();
  QCOMPARE(info.id.notebookId, nbId);
}

void TestHistoryListModel::test_multiNotebookMerge() {
  QString nbId1 = createTestNotebook(QStringLiteral("merge_nb1"));
  QVERIFY(!nbId1.isEmpty());
  QString nbId2 = createTestNotebook(QStringLiteral("merge_nb2"));
  QVERIFY(!nbId2.isEmpty());

  // Create 1 file in each notebook.
  QString fileA =
      m_notebookService->createFile(nbId1, QString(), QStringLiteral("fileA.md"));
  QVERIFY(!fileA.isEmpty());
  QString fileB =
      m_notebookService->createFile(nbId2, QString(), QStringLiteral("fileB.md"));
  QVERIFY(!fileB.isEmpty());

  // Open buffers (adds history).
  QString bufA = m_bufferService->openBuffer(nbId1, QStringLiteral("fileA.md"));
  QVERIFY(!bufA.isEmpty());

  // Small delay to ensure different timestamps.
  QTest::qWait(50);

  QString bufB = m_bufferService->openBuffer(nbId2, QStringLiteral("fileB.md"));
  QVERIFY(!bufB.isEmpty());

  HistoryListModel model(m_notebookService);
  model.loadHistory();
  QCOMPARE(model.rowCount(), 2);

  // Items should come from different notebooks.
  NodeIdentifier id0 = model.nodeIdFromIndex(model.index(0, 0));
  NodeIdentifier id1 = model.nodeIdFromIndex(model.index(1, 0));
  QVERIFY(id0.notebookId != id1.notebookId);

  // Most recent first (fileB opened last).
  QCOMPARE(id0.notebookId, nbId2);
  QCOMPARE(id1.notebookId, nbId1);
}

void TestHistoryListModel::test_nodeIdFromIndex() {
  QString nbId = createTestNotebook(QStringLiteral("nodeid_nb"));
  QVERIFY(!nbId.isEmpty());

  QString fileId =
      m_notebookService->createFile(nbId, QString(), QStringLiteral("test.md"));
  QVERIFY(!fileId.isEmpty());
  QString buf = m_bufferService->openBuffer(nbId, QStringLiteral("test.md"));
  QVERIFY(!buf.isEmpty());

  HistoryListModel model(m_notebookService);
  model.loadHistory();
  QVERIFY(model.rowCount() > 0);

  NodeIdentifier nodeId = model.nodeIdFromIndex(model.index(0, 0));
  QVERIFY(!nodeId.notebookId.isEmpty());
  QVERIFY(!nodeId.relativePath.isEmpty());

  // Invalid index returns invalid NodeIdentifier.
  NodeIdentifier badId = model.nodeIdFromIndex(QModelIndex());
  QVERIFY(!badId.isValid());
}

void TestHistoryListModel::test_indexFromNodeId() {
  QString nbId = createTestNotebook(QStringLiteral("indexfrom_nb"));
  QVERIFY(!nbId.isEmpty());

  QString fileId =
      m_notebookService->createFile(nbId, QString(), QStringLiteral("lookup.md"));
  QVERIFY(!fileId.isEmpty());
  QString buf = m_bufferService->openBuffer(nbId, QStringLiteral("lookup.md"));
  QVERIFY(!buf.isEmpty());

  HistoryListModel model(m_notebookService);
  model.loadHistory();
  QVERIFY(model.rowCount() > 0);

  NodeIdentifier nodeId = model.nodeIdFromIndex(model.index(0, 0));
  QModelIndex idx = model.indexFromNodeId(nodeId);
  QVERIFY(idx.isValid());
  QCOMPARE(idx.row(), 0);
}

void TestHistoryListModel::test_dataRoles() {
  QString nbId = createTestNotebook(QStringLiteral("roles_nb"));
  QVERIFY(!nbId.isEmpty());

  QString fileId =
      m_notebookService->createFile(nbId, QString(), QStringLiteral("roles.md"));
  QVERIFY(!fileId.isEmpty());
  QString buf = m_bufferService->openBuffer(nbId, QStringLiteral("roles.md"));
  QVERIFY(!buf.isEmpty());

  HistoryListModel model(m_notebookService);
  model.loadHistory();
  QVERIFY(model.rowCount() > 0);

  QModelIndex idx = model.index(0, 0);

  // DisplayRole returns the file name.
  QVariant displayVal = model.data(idx, Qt::DisplayRole);
  QCOMPARE(displayVal.toString(), QStringLiteral("roles.md"));

  // ToolTipRole returns the relative path.
  QVariant tooltipVal = model.data(idx, Qt::ToolTipRole);
  QCOMPARE(tooltipVal.toString(), QStringLiteral("roles.md"));

  // PathRole returns the relative path.
  QVariant pathVal = model.data(idx, INodeListModel::PathRole);
  QCOMPARE(pathVal.toString(), QStringLiteral("roles.md"));

  // IsFolderRole returns false.
  QVariant folderVal = model.data(idx, INodeListModel::IsFolderRole);
  QCOMPARE(folderVal.toBool(), false);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestHistoryListModel)
#include "test_historylistmodel.moc"
