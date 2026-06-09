// Tests for controller readonly state propagation (T17).
//
// Ensures that controllers correctly propagate Buffer2::isReadOnly()
// to BufferState.readOnly instead of hardcoding it as false.
//
// Cases:
//   * markdownEditorReadOnlyBuffer: Markdown editor controller receives a read-only
//     buffer; prepareBufferState returns BufferState with readOnly=true.
//   * markdownEditorWritableBuffer: Markdown editor controller receives a writable
//     buffer; prepareBufferState returns BufferState with readOnly=false.
//   * textViewWindowReadOnlyBuffer: Text view window controller receives a read-only
//     buffer; prepareBufferState returns BufferState with readOnly=true.
//   * textViewWindowWritableBuffer: Text view window controller receives a writable
//     buffer; prepareBufferState returns BufferState with readOnly=false.

#include <QtTest>

#include <QByteArray>
#include <QString>

#include <controllers/markdowneditorcontroller.h>
#include <controllers/textviewwindowcontroller.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/buffercoreservice.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestControllersReadOnlyState : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void markdownEditorReadOnlyBuffer();
  void markdownEditorWritableBuffer();
  void textViewWindowReadOnlyBuffer();
  void textViewWindowWritableBuffer();

private:
  VxCoreContextHandle m_ctx = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  BufferService *m_bufferService = nullptr;
  ServiceLocator m_services;
  TempDirFixture m_workTemp;

  // Counter so each test gets its own notebook directory (avoids vxcore
  // "already exists" failures when multiple tests share the same TempDirFixture).
  int m_notebookCounter = 0;

  // Create and open a notebook, returning its ID; on failure, return empty string.
  QString createTestNotebook();

  // Open a buffer for the given notebook and relative path, return Buffer2.
  // If p_readOnly is true, set the notebook as read-only before opening.
  Buffer2 openTestBuffer(const QString &p_notebookId, const QString &p_relativePath,
                         bool p_readOnly);
};

void TestControllersReadOnlyState::initTestCase() {
  QVERIFY(m_workTemp.isValid());

  // CRITICAL: enable test mode BEFORE vxcore_context_create
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create(nullptr, &m_ctx);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_ctx != nullptr);

  // Register services. ServiceLocator stores non-owning pointers; lifetime is
  // managed manually in cleanupTestCase (services MUST be deleted BEFORE the
  // vxcore context).
  m_hookMgr = new HookManager();
  m_notebookService = new NotebookCoreService(m_ctx);
  m_bufferService = new BufferService(m_ctx, m_hookMgr, AutoSavePolicy::AutoSave);

  m_services.registerService<HookManager>(m_hookMgr);
  m_services.registerService<NotebookCoreService>(m_notebookService);
  m_services.registerService<BufferService>(m_bufferService);
}

void TestControllersReadOnlyState::cleanupTestCase() {
  // Reverse-dependency order: services first, then vxcore context.
  delete m_bufferService;
  m_bufferService = nullptr;

  delete m_notebookService;
  m_notebookService = nullptr;

  delete m_hookMgr;
  m_hookMgr = nullptr;

  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

QString TestControllersReadOnlyState::createTestNotebook() {
  auto *notebookService = m_services.get<NotebookCoreService>();
  // Unique path per call so multiple tests do not collide on the same dir.
  const QString notebookPath =
      m_workTemp.filePath(QStringLiteral("test_notebook_%1").arg(++m_notebookCounter));
  const QString configJson =
      QStringLiteral(R"({"name":"Test Notebook","description":"T17 test","version":"1"})");

  const QString notebookId =
      notebookService->createNotebook(notebookPath, configJson, NotebookType::Bundled);
  // createNotebook implicitly opens the notebook; no separate openNotebook call needed.
  return notebookId;
}

Buffer2 TestControllersReadOnlyState::openTestBuffer(const QString &p_notebookId,
                                                     const QString &p_relativePath,
                                                     bool p_readOnly) {
  auto *bufferService = m_services.get<BufferService>();

  // If p_readOnly, flip the notebook to read-only via the C API. The Qt
  // service layer intentionally does NOT expose a setReadOnly wrapper; the
  // canonical entry points are vxcore_notebook_set_read_only and
  // NotebookCoreService::openNotebookEx (with {"readOnly":true} options).
  // Direct C call is the simplest seam for an already-open notebook.
  if (p_readOnly) {
    VxCoreError err = vxcore_notebook_set_read_only(m_ctx, p_notebookId.toUtf8().constData(), true);
    Q_UNUSED(err);
  }

  NodeIdentifier nodeId;
  nodeId.notebookId = p_notebookId;
  nodeId.relativePath = p_relativePath;

  return bufferService->openBuffer(nodeId);
}

void TestControllersReadOnlyState::markdownEditorReadOnlyBuffer() {
  QString notebookId = createTestNotebook();
  QVERIFY(!notebookId.isEmpty());

  // Verify the notebook is registered (modern equivalent of the deleted
  // getNotebook(id) pointer check).
  auto *notebookService = m_services.get<NotebookCoreService>();
  QVERIFY(!notebookService->getNotebookConfig(notebookId).isEmpty());

  // Create a simple markdown file at the notebook root (folderPath="").
  QString fileId = notebookService->createFile(notebookId, QString(), QStringLiteral("test.md"));
  QVERIFY(!fileId.isEmpty());

  // Open as read-only buffer
  Buffer2 buffer = openTestBuffer(notebookId, QStringLiteral("test.md"), true);
  QVERIFY(buffer.isValid());
  QVERIFY(buffer.isReadOnly()); // Verify buffer is actually read-only

  // Test MarkdownEditorController::prepareBufferState
  MarkdownEditorController::BufferState state =
      MarkdownEditorController::prepareBufferState(buffer);

  // The state should reflect the buffer's read-only status
  QVERIFY(state.valid);
  QVERIFY(state.readOnly); // This is the key assertion: readOnly should be TRUE
}

void TestControllersReadOnlyState::markdownEditorWritableBuffer() {
  QString notebookId = createTestNotebook();
  QVERIFY(!notebookId.isEmpty());

  auto *notebookService = m_services.get<NotebookCoreService>();
  QVERIFY(!notebookService->getNotebookConfig(notebookId).isEmpty());

  // Create a simple markdown file at the notebook root.
  QString fileId = notebookService->createFile(notebookId, QString(), QStringLiteral("test2.md"));
  QVERIFY(!fileId.isEmpty());

  // Open as writable buffer (don't set read-only)
  Buffer2 buffer = openTestBuffer(notebookId, QStringLiteral("test2.md"), false);
  QVERIFY(buffer.isValid());
  QVERIFY(!buffer.isReadOnly()); // Verify buffer is writable

  // Test MarkdownEditorController::prepareBufferState
  MarkdownEditorController::BufferState state =
      MarkdownEditorController::prepareBufferState(buffer);

  // The state should reflect the buffer's writable status
  QVERIFY(state.valid);
  QVERIFY(!state.readOnly); // This is the key assertion: readOnly should be FALSE
}

void TestControllersReadOnlyState::textViewWindowReadOnlyBuffer() {
  QString notebookId = createTestNotebook();
  QVERIFY(!notebookId.isEmpty());

  auto *notebookService = m_services.get<NotebookCoreService>();
  QVERIFY(!notebookService->getNotebookConfig(notebookId).isEmpty());

  // Create a simple text file at the notebook root.
  QString fileId = notebookService->createFile(notebookId, QString(), QStringLiteral("test.txt"));
  QVERIFY(!fileId.isEmpty());

  // Open as read-only buffer
  Buffer2 buffer = openTestBuffer(notebookId, QStringLiteral("test.txt"), true);
  QVERIFY(buffer.isValid());
  QVERIFY(buffer.isReadOnly()); // Verify buffer is actually read-only

  // Test TextViewWindowController::prepareBufferState
  TextViewWindowController::BufferState state =
      TextViewWindowController::prepareBufferState(buffer);

  // The state should reflect the buffer's read-only status
  QVERIFY(state.valid);
  QVERIFY(state.readOnly); // This is the key assertion: readOnly should be TRUE
}

void TestControllersReadOnlyState::textViewWindowWritableBuffer() {
  QString notebookId = createTestNotebook();
  QVERIFY(!notebookId.isEmpty());

  auto *notebookService = m_services.get<NotebookCoreService>();
  QVERIFY(!notebookService->getNotebookConfig(notebookId).isEmpty());

  // Create a simple text file at the notebook root.
  QString fileId = notebookService->createFile(notebookId, QString(), QStringLiteral("test2.txt"));
  QVERIFY(!fileId.isEmpty());

  // Open as writable buffer (don't set read-only)
  Buffer2 buffer = openTestBuffer(notebookId, QStringLiteral("test2.txt"), false);
  QVERIFY(buffer.isValid());
  QVERIFY(!buffer.isReadOnly()); // Verify buffer is writable

  // Test TextViewWindowController::prepareBufferState
  TextViewWindowController::BufferState state =
      TextViewWindowController::prepareBufferState(buffer);

  // The state should reflect the buffer's writable status
  QVERIFY(state.valid);
  QVERIFY(!state.readOnly); // This is the key assertion: readOnly should be FALSE
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestControllersReadOnlyState)
#include "test_controllers_readonly_state.moc"
