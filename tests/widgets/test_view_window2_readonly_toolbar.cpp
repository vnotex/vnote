// Exercise production toolbar actions against writable and read-only notebook buffers.

#include <QAction>
#include <QApplication>
#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QToolBar>
#include <QtTest>

#include <core/configmgr2.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/configcoreservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

#include <widgets/viewwindow2.h>

using namespace vnotex;

namespace tests {

// Expose the production action factory without constructing a full editor.
class TestViewWindow2 : public vnotex::ViewWindow2 {
  Q_OBJECT
public:
  TestViewWindow2(vnotex::ServiceLocator &p_services, const vnotex::Buffer2 &p_buffer,
                  QWidget *p_parent = nullptr)
      : vnotex::ViewWindow2(p_services, p_buffer, p_parent) {}

  // Pure virtual implementations (no-op for this test).
  void setMode(vnotex::ViewWindowMode p_mode) override { m_mode = p_mode; }
  void syncEditorFromBuffer() override {}
  void setModified(bool) override {}
  QString getLatestContent() const override { return {}; }
  void scrollUp() override {}
  void scrollDown() override {}
  void zoom(bool) override {}

  using ViewWindow2::addAction;

  // Test seam: flip dirty state and trigger the Save action's refresh lambda.
  void setDirty(bool p_dirty) {
    m_editorDirty = p_dirty;
    emit statusChanged();
  }

  // Test seam: switch view mode and trigger the Insert action's refresh.
  void switchMode(vnotex::ViewWindowMode p_mode) {
    m_mode = p_mode;
    emit modeChanged();
  }
};

class TestViewWindow2ReadOnlyToolbar : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testSaveActionDisabledForReadOnlyBuffer();
  void testSaveActionEnabledForWritableBuffer();
  void testInsertActionDisabledForReadOnlyBufferInEditMode();
  void testInsertActionEnabledForWritableBufferInEditMode();

private:
  vnotex::Buffer2 openBuffer(const QString &p_relativePath);
  void setNotebookReadOnly(bool p_readOnly);

  ConfigCoreService *m_configCore = nullptr;
  ConfigMgr2 *m_config = nullptr;
  VxCoreContextHandle m_context = nullptr;
  vnotex::BufferService *m_bufferService = nullptr;
  vnotex::HookManager *m_hookMgr = nullptr;
  vnotex::NotebookCoreService *m_notebookService = nullptr;
  vnotex::ServiceLocator m_services;
  TempDirFixture m_tempDir;
  QString m_notebookId;
};

void TestViewWindow2ReadOnlyToolbar::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_configCore = new ConfigCoreService(m_context);
  m_config = new ConfigMgr2(m_configCore);
  m_config->init();
  m_services.registerService(m_configCore);
  m_services.registerService(m_config);

  m_notebookService = new vnotex::NotebookCoreService(m_context, this);
  m_hookMgr = new vnotex::HookManager(this);
  // AutoSavePolicy::None keeps the autosave timer dormant — we never want
  // background saves competing with the test's direct API calls.
  m_bufferService =
      new vnotex::BufferService(m_context, m_hookMgr, vnotex::AutoSavePolicy::None, this);

  m_services.registerService<vnotex::BufferService>(m_bufferService);
  m_services.registerService<vnotex::NotebookCoreService>(m_notebookService);
  m_services.registerService<vnotex::HookManager>(m_hookMgr);

  const QString nbPath = m_tempDir.filePath(QStringLiteral("readonly_toolbar_test"));
  const QString configJson =
      QStringLiteral(R"({"name": "RO Toolbar Test", "description": "Test", "version": "1"})");
  m_notebookId =
      m_notebookService->createNotebook(nbPath, configJson, vnotex::NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
}

void TestViewWindow2ReadOnlyToolbar::cleanupTestCase() {
  // Always reset RO state before tear-down so the notebook close path is not
  // blocked by VXCORE_ERR_READ_ONLY guards.
  if (!m_notebookId.isEmpty() && m_context) {
    vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), false);
  }

  if (m_bufferService) {
    m_bufferService->shutdown(2000);
  }
  if (!m_notebookId.isEmpty() && m_notebookService) {
    m_notebookService->closeNotebook(m_notebookId);
  }

  delete m_bufferService;
  m_bufferService = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  delete m_notebookService;
  m_notebookService = nullptr;

  delete m_config;
  m_config = nullptr;
  delete m_configCore;
  m_configCore = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestViewWindow2ReadOnlyToolbar::cleanup() {
  // Reset RO between subtests AND close any opened buffers so each subtest
  // starts from a clean slate.
  if (!m_notebookId.isEmpty()) {
    vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), false);
  }
  const QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &v : buffers) {
    const QString id = v.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_bufferService->closeBuffer(id);
    }
  }
}

vnotex::Buffer2 TestViewWindow2ReadOnlyToolbar::openBuffer(const QString &p_relativePath) {
  return m_bufferService->openBuffer(vnotex::NodeIdentifier{m_notebookId, p_relativePath});
}

void TestViewWindow2ReadOnlyToolbar::setNotebookReadOnly(bool p_readOnly) {
  VxCoreError err =
      vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), p_readOnly);
  QCOMPARE(err, VXCORE_OK);
}

// =============================================================================
// Subtest 1: Save action stays disabled on a RO buffer even after markDirty.
// =============================================================================
void TestViewWindow2ReadOnlyToolbar::testSaveActionDisabledForReadOnlyBuffer() {
  const QString relPath = QStringLiteral("ro_save.md");
  const QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  // Seed a writable baseline on disk, close, THEN flip RO and re-open: a
  // buffer's read-only state is resolved when it is opened.
  vnotex::Buffer2 seed = openBuffer(relPath);
  QVERIFY(seed.isValid());
  QVERIFY(seed.setContentRaw(QByteArray("original\n")));
  QVERIFY(seed.save());
  m_bufferService->closeBuffer(seed.id());

  setNotebookReadOnly(true);
  vnotex::Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);
  QToolBar toolbar(&view);
  QAction *saveAction = view.addAction(&toolbar, ViewWindowToolBarHelper2::Save);

  // Mark buffer dirty; Save MUST stay disabled because notebook is RO.
  view.setDirty(true);
  QVERIFY(!saveAction->isEnabled());
}

// =============================================================================
// Subtest 2: Save action enables on a writable buffer once dirty.
// =============================================================================
void TestViewWindow2ReadOnlyToolbar::testSaveActionEnabledForWritableBuffer() {
  const QString relPath = QStringLiteral("rw_save.md");
  const QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  vnotex::Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("original\n")));
  QVERIFY(buf.save());
  QVERIFY(!buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);
  QToolBar toolbar(&view);
  QAction *saveAction = view.addAction(&toolbar, ViewWindowToolBarHelper2::Save);

  // Pristine + writable: Save action starts disabled (nothing to save).
  QVERIFY(!saveAction->isEnabled());

  // Mark dirty → Save should enable.
  view.setDirty(true);
  QVERIFY(saveAction->isEnabled());

  // Clear dirty → Save disables again.
  view.setDirty(false);
  QVERIFY(!saveAction->isEnabled());
}

// =============================================================================
// Subtest 3: Insert action stays disabled in Edit mode on a RO buffer.
// =============================================================================
void TestViewWindow2ReadOnlyToolbar::testInsertActionDisabledForReadOnlyBufferInEditMode() {
  const QString relPath = QStringLiteral("ro_insert.md");
  const QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  // Read-only is resolved at open time, so flip the notebook BEFORE opening.
  setNotebookReadOnly(true);
  vnotex::Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);
  QToolBar toolbar(&view);
  QAction *insertAction = view.addAction(&toolbar, ViewWindowToolBarHelper2::TypeBold);

  // Initially: not visible (Read mode default) and disabled (RO).
  QVERIFY(!insertAction->isVisible());
  QVERIFY(!insertAction->isEnabled());

  // Switch to Edit mode. Visibility should follow mode; enabled state must
  // still be false because the notebook is RO.
  view.switchMode(vnotex::ViewWindowMode::Edit);
  QVERIFY(insertAction->isVisible());
  QVERIFY(!insertAction->isEnabled());

  // Switch back to Read — both go false.
  view.switchMode(vnotex::ViewWindowMode::Read);
  QVERIFY(!insertAction->isVisible());
  QVERIFY(!insertAction->isEnabled());
}

// =============================================================================
// Subtest 4: Insert action enables in Edit mode on a writable buffer.
// =============================================================================
void TestViewWindow2ReadOnlyToolbar::testInsertActionEnabledForWritableBufferInEditMode() {
  const QString relPath = QStringLiteral("rw_insert.md");
  const QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  vnotex::Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(!buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);
  QToolBar toolbar(&view);
  QAction *insertAction = view.addAction(&toolbar, ViewWindowToolBarHelper2::TypeBold);

  // Edit mode + writable: visible and enabled.
  view.switchMode(vnotex::ViewWindowMode::Edit);
  QVERIFY(insertAction->isVisible());
  QVERIFY(insertAction->isEnabled());

  // Read mode hides and disables.
  view.switchMode(vnotex::ViewWindowMode::Read);
  QVERIFY(!insertAction->isVisible());
  QVERIFY(!insertAction->isEnabled());
}

} // namespace tests

QTEST_MAIN(tests::TestViewWindow2ReadOnlyToolbar)
#include "test_view_window2_readonly_toolbar.moc"
