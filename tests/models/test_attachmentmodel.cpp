#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include <vxcore/vxcore.h>

#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <models/attachmentlistmodel.h>

namespace tests {

class TestAttachmentModel : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testSetBufferPopulates();
  void testEmptyAttachments();
  void testFlagsIncludeEditable();
  void testSetDataRenameUsesReturnedName();
  void testSetDataEmptyNameReturnsFalse();
  void testSetDataUnchangedNameReturnsFalse();

private:
  void reopenCleanBuffer();
  QString addAttachment(const QString &p_fileName, const QByteArray &p_content = QByteArray("x"));

  QTemporaryDir m_tempDir;
  VxCoreContextHandle m_context = nullptr;
  vnotex::NotebookCoreService *m_notebookService = nullptr;
  vnotex::HookManager *m_hookMgr = nullptr;
  vnotex::BufferService *m_bufferService = nullptr;
  QString m_notebookId;
  vnotex::Buffer2 m_buffer;
  vnotex::AttachmentListModel *m_model = nullptr;
};

void TestAttachmentModel::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new vnotex::NotebookCoreService(m_context, this);
  m_hookMgr = new vnotex::HookManager(this);
  m_bufferService =
      new vnotex::BufferService(m_context, m_hookMgr, vnotex::AutoSavePolicy::AutoSave, this);
  m_model = new vnotex::AttachmentListModel(this);

  QString nbPath = m_tempDir.filePath(QStringLiteral("attachment_model_test"));
  QString configJson =
      QStringLiteral(R"({"name":"AttModel","description":"Test","version":"1"})");
  m_notebookId = m_notebookService->createNotebook(nbPath, configJson, vnotex::NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());

  QString fileId = m_notebookService->createFile(m_notebookId, QString(), QStringLiteral("test.md"));
  QVERIFY(!fileId.isEmpty());
}

void TestAttachmentModel::cleanupTestCase() {
  if (m_buffer.isValid()) {
    m_bufferService->closeBuffer(m_buffer.id());
  }

  delete m_bufferService;
  m_bufferService = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  delete m_notebookService;
  m_notebookService = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestAttachmentModel::cleanup() {
  if (m_buffer.isValid()) {
    m_bufferService->closeBuffer(m_buffer.id());
    m_buffer = vnotex::Buffer2();
  }
}

void TestAttachmentModel::reopenCleanBuffer() {
  if (m_buffer.isValid()) {
    m_bufferService->closeBuffer(m_buffer.id());
    m_buffer = vnotex::Buffer2();
  }

  m_buffer = m_bufferService->openBuffer(vnotex::NodeIdentifier{m_notebookId, QStringLiteral("test.md")});
  QVERIFY(m_buffer.isValid());

  const QJsonArray attachments = m_buffer.listAttachments();
  for (const auto &val : attachments) {
    const QString name = val.toString();
    if (!name.isEmpty()) {
      QVERIFY(m_buffer.deleteAttachment(name));
    }
  }
}

QString TestAttachmentModel::addAttachment(const QString &p_fileName, const QByteArray &p_content) {
  QString srcPath = m_tempDir.filePath(p_fileName);
  QFile src(srcPath);
  if (!src.open(QIODevice::WriteOnly)) {
    return QString();
  }

  if (src.write(p_content) <= 0) {
    src.close();
    return QString();
  }

  src.close();

  return m_buffer.insertAttachment(srcPath);
}

void TestAttachmentModel::testSetBufferPopulates() {
  reopenCleanBuffer();
  QString name1 = addAttachment(QStringLiteral("alpha.txt"));
  QString name2 = addAttachment(QStringLiteral("beta.png"));

  m_model->setBuffer(&m_buffer);

  QCOMPARE(m_model->rowCount(), 2);
  QStringList names;
  for (int i = 0; i < m_model->rowCount(); ++i) {
    QModelIndex idx = m_model->index(i, 0);
    names.append(m_model->data(idx, Qt::DisplayRole).toString());
  }

  QStringList expected{name1, name2};
  names.sort();
  expected.sort();
  QCOMPARE(names, expected);
}

void TestAttachmentModel::testEmptyAttachments() {
  reopenCleanBuffer();
  m_model->setBuffer(&m_buffer);
  QCOMPARE(m_model->rowCount(), 0);
}

void TestAttachmentModel::testFlagsIncludeEditable() {
  reopenCleanBuffer();
  addAttachment(QStringLiteral("editable.txt"));
  m_model->setBuffer(&m_buffer);

  QModelIndex idx = m_model->index(0, 0);
  Qt::ItemFlags f = m_model->flags(idx);
  QVERIFY(f.testFlag(Qt::ItemIsEditable));
  QVERIFY(f.testFlag(Qt::ItemIsSelectable));
  QVERIFY(f.testFlag(Qt::ItemIsEnabled));
}

void TestAttachmentModel::testSetDataRenameUsesReturnedName() {
  reopenCleanBuffer();
  QString oldName = addAttachment(QStringLiteral("from.txt"));
  addAttachment(QStringLiteral("to.txt"));
  m_model->setBuffer(&m_buffer);

  int row = -1;
  for (int i = 0; i < m_model->rowCount(); ++i) {
    if (m_model->attachmentAt(i) == oldName) {
      row = i;
      break;
    }
  }

  QVERIFY(row >= 0);

  const QString requestedName = QStringLiteral("to.txt");
  QModelIndex idx = m_model->index(row, 0);
  QVERIFY(m_model->setData(idx, requestedName, Qt::EditRole));

  const QString actualName = m_model->attachmentAt(row);
  QVERIFY(!actualName.isEmpty());
  QVERIFY(actualName != oldName);
  QVERIFY(actualName != requestedName);

  QStringList current;
  const QJsonArray attachments = m_buffer.listAttachments();
  for (const auto &val : attachments) {
    current.append(val.toString());
  }

  QVERIFY(current.contains(actualName));
  QVERIFY(!current.contains(oldName));
}

void TestAttachmentModel::testSetDataEmptyNameReturnsFalse() {
  reopenCleanBuffer();
  QString oldName = addAttachment(QStringLiteral("empty-name.txt"));
  m_model->setBuffer(&m_buffer);

  QModelIndex idx = m_model->index(0, 0);
  QVERIFY(!m_model->setData(idx, QStringLiteral("   \t  "), Qt::EditRole));
  QCOMPARE(m_model->attachmentAt(0), oldName);
}

void TestAttachmentModel::testSetDataUnchangedNameReturnsFalse() {
  reopenCleanBuffer();
  QString oldName = addAttachment(QStringLiteral("same-name.txt"));
  m_model->setBuffer(&m_buffer);

  QModelIndex idx = m_model->index(0, 0);
  QVERIFY(!m_model->setData(idx, oldName, Qt::EditRole));
  QCOMPARE(m_model->attachmentAt(0), oldName);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestAttachmentModel)
#include "test_attachmentmodel.moc"
