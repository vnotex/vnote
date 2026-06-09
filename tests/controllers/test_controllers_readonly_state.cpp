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
  vxcore_context *m_ctx = nullptr;
  ServiceLocator m_services;
  TempDirFixture m_workTemp;

  // Create and open a notebook, returning its ID; on failure, return empty string.
  QString createTestNotebook();

  // Open a buffer for the given notebook and relative path, return Buffer2.
  // If p_readOnly is true, set the notebook as read-only before opening.
  Buffer2 openTestBuffer(const QString &p_notebookId, const QString &p_relativePath,
                         bool p_readOnly);
};

void TestControllersReadOnlyState::initTestCase() {
  // CRITICAL: enable test mode BEFORE vxcore_context_create
  vxcore_set_test_mode(1);

  m_ctx = vxcore_context_create();
  QVERIFY(m_ctx != nullptr);

  // Register services
  m_services.registerService<NotebookCoreService>(std::make_unique<NotebookCoreService>(m_ctx));
  m_services.registerService<BufferCoreService>(std::make_unique<BufferCoreService>(m_ctx));
  m_services.registerService<HookManager>(std::make_unique<HookManager>());
  m_services.registerService<BufferService>(
      std::make_unique<BufferService>(m_ctx, m_services.get<HookManager>()));
}

void TestControllersReadOnlyState::cleanupTestCase() {
  vxcore_context_destroy(m_ctx);
  m_ctx = nullptr;
}

QString TestControllersReadOnlyState::createTestNotebook() {
  auto *notebookService = m_services.get<NotebookCoreService>();
  QString notebookPath = m_workTemp.filePath(QStringLiteral("test_notebook"));

  QString notebookId =
      notebookService->createNotebook(notebookPath, QStringLiteral("Test Notebook"), QString());
  if (notebookId.isEmpty()) {
    return QString();
  }

  notebookService->openNotebook(notebookId);
  return notebookId;
}

Buffer2 TestControllersReadOnlyState::openTestBuffer(const QString &p_notebookId,
                                                     const QString &p_relativePath,
                                                     bool p_readOnly) {
  auto *notebookService = m_services.get<NotebookCoreService>();
  auto *bufferService = m_services.get<BufferService>();

  // If p_readOnly, set the notebook read-only via vxcore
  if (p_readOnly) {
    auto notebook = notebookService->getNotebook(p_notebookId);
    if (notebook) {
      notebook->SetReadOnly(true);
    }
  }

  NodeIdentifier nodeId;
  nodeId.notebookId = p_notebookId;
  nodeId.relativePath = p_relativePath;

  return bufferService->openBuffer(nodeId);
}

void TestControllersReadOnlyState::markdownEditorReadOnlyBuffer() {
  QString notebookId = createTestNotebook();
  QVERIFY(!notebookId.isEmpty());

  // Create a test file
  auto *notebookService = m_services.get<NotebookCoreService>();
  auto notebook = notebookService->getNotebook(notebookId);
  QVERIFY(notebook);

  // Create a simple markdown file
  QString filePath = notebookService->createFile(notebookId, QStringLiteral("test.md"), QString());
  QVERIFY(!filePath.isEmpty());

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
  auto notebook = notebookService->getNotebook(notebookId);
  QVERIFY(notebook);

  // Create a simple markdown file
  QString filePath = notebookService->createFile(notebookId, QStringLiteral("test2.md"), QString());
  QVERIFY(!filePath.isEmpty());

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
  auto notebook = notebookService->getNotebook(notebookId);
  QVERIFY(notebook);

  // Create a simple text file
  QString filePath = notebookService->createFile(notebookId, QStringLiteral("test.txt"), QString());
  QVERIFY(!filePath.isEmpty());

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
  auto notebook = notebookService->getNotebook(notebookId);
  QVERIFY(notebook);

  // Create a simple text file
  QString filePath =
      notebookService->createFile(notebookId, QStringLiteral("test2.txt"), QString());
  QVERIFY(!filePath.isEmpty());

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
