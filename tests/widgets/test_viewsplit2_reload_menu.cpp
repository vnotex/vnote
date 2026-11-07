// Exercise production ViewWindow2 reload/save behavior with real notebook buffers.

#include <QApplication>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTextStream>
#include <QTimer>
#include <QtTest>

#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

#include <widgets/viewwindow2.h>

using namespace vnotex;

namespace tests {

// Minimal concrete ViewWindow2 subclass for testing reload().
// All pure virtuals are implemented as counters/state we can inspect.
class TestViewWindow2 : public vnotex::ViewWindow2 {
  Q_OBJECT
public:
  TestViewWindow2(vnotex::ServiceLocator &p_services, const vnotex::Buffer2 &p_buffer,
                  QWidget *p_parent = nullptr)
      : vnotex::ViewWindow2(p_services, p_buffer, p_parent),
        m_latestContent(p_buffer.decode(p_buffer.getContentRaw())) {}

  ~TestViewWindow2() override {
    if (auto *buffers = getServices().get<BufferService>()) {
      buffers->unregisterActiveWriter(getBuffer().id(), reinterpret_cast<quintptr>(this));
    }
  }

  // ============ Pure virtual implementations ============
  void setMode(vnotex::ViewWindowMode p_mode) override {
    ++setModeCallCount;
    m_mode = p_mode;
  }
  void syncEditorFromBuffer() override {
    ++syncEditorCallCount;
    m_latestContent = getBuffer().decode(getBuffer().getContentRaw());
  }
  void setModified(bool p_modified) override { m_editorDirty = p_modified; }
  QString getLatestContent() const override { return m_latestContent; }
  void scrollUp() override {}
  void scrollDown() override {}
  void zoom(bool) override {}

  // ============ Virtual overrides (for counter tracking) ============
  ViewPositionState capturePositionState() const override {
    ++captureScrollCallCount;
    ViewPositionState s;
    s.m_scrollValue = 0; // Make the state "valid" so restorePositionState() runs.
    s.m_cursorLine = fakeCursorLine;
    s.m_cursorPositionInBlock = fakeCursorOffset;
    return s;
  }
  void restorePositionState(const ViewPositionState &p_state) override {
    ++restoreScrollCallCount;
    restoredCursorLine = p_state.m_cursorLine;
    restoredCursorOffset = p_state.m_cursorPositionInBlock;
  }

  // ============ Test seams ============
  void setInitialMode(vnotex::ViewWindowMode p_mode) { m_mode = p_mode; }

  // ============ Helpers exposing protected state to tests ============
  vnotex::ViewWindowMode currentMode() const { return m_mode; }

  // ============ Counters ============
  mutable int setModeCallCount = 0;
  mutable int syncEditorCallCount = 0;
  mutable int captureScrollCallCount = 0;
  mutable int restoreScrollCallCount = 0;
  // Cursor channel: what capture reports, and what restore was handed back.
  int fakeCursorLine = -1;
  int fakeCursorOffset = -1;
  mutable int restoredCursorLine = -1;
  mutable int restoredCursorOffset = -1;
  QString m_latestContent;
};

class TestViewSplit2ReloadMenu : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testEmptyPathReturnsFalse();
  void testCleanReloadPicksUpDiskContent();
  void testDirtyCancelKeepsState();
  void testDirtyDiscardReplacesContent();
  void testDirtySaveSavesThenReloads();
  void testModePreservedAcrossReload();
  void testCursorStateHandedBackOnReload();
  void testCursorRestoreSkippedWhenReloadFails();

private:
  vnotex::Buffer2 openBuffer(const QString &p_relativePath);
  void writeFile(const QString &p_path, const QByteArray &p_content);
  QByteArray readFile(const QString &p_path);
  static QTimer *armMessageBoxClick(QMessageBox::StandardButton p_button);

  VxCoreContextHandle m_context = nullptr;
  vnotex::BufferService *m_bufferService = nullptr;
  vnotex::HookManager *m_hookMgr = nullptr;
  vnotex::NotebookCoreService *m_notebookService = nullptr;
  vnotex::ServiceLocator m_services;
  TempDirFixture m_tempDir;
  QString m_notebookId;
};

void TestViewSplit2ReloadMenu::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  // CRITICAL: enable test mode BEFORE any vxcore_context_create.
  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new vnotex::NotebookCoreService(m_context, this);
  m_hookMgr = new vnotex::HookManager(this);
  m_bufferService =
      new vnotex::BufferService(m_context, m_hookMgr, vnotex::AutoSavePolicy::None, this);

  m_services.registerService<vnotex::BufferService>(m_bufferService);
  m_services.registerService<vnotex::NotebookCoreService>(m_notebookService);
  m_services.registerService<vnotex::HookManager>(m_hookMgr);

  const QString nbPath = m_tempDir.filePath(QStringLiteral("reload_menu_test"));
  const QString configJson =
      QStringLiteral(R"({"name": "Reload Menu Test", "description": "Test", "version": "1"})");
  m_notebookId =
      m_notebookService->createNotebook(nbPath, configJson, vnotex::NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
}

void TestViewSplit2ReloadMenu::cleanupTestCase() {
  m_bufferService->shutdown(2000);
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

void TestViewSplit2ReloadMenu::cleanup() {
  // Close all open buffers between tests.
  QJsonArray buffers = m_bufferService->listBuffers();
  for (const auto &bufVal : buffers) {
    QString id = bufVal.toObject()[QStringLiteral("id")].toString();
    if (!id.isEmpty()) {
      m_bufferService->closeBuffer(id);
    }
  }
}

vnotex::Buffer2 TestViewSplit2ReloadMenu::openBuffer(const QString &p_relativePath) {
  return m_bufferService->openBuffer(vnotex::NodeIdentifier{m_notebookId, p_relativePath});
}

void TestViewSplit2ReloadMenu::writeFile(const QString &p_path, const QByteArray &p_content) {
  QFile f(p_path);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QVERIFY(f.write(p_content) == p_content.size());
  f.close();
}

QByteArray TestViewSplit2ReloadMenu::readFile(const QString &p_path) {
  QFile f(p_path);
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }
  return f.readAll();
}

// Arms a one-shot timer that polls every 50ms for a visible QMessageBox at
// any QApplication top-level and clicks the requested standard button.
QTimer *TestViewSplit2ReloadMenu::armMessageBoxClick(QMessageBox::StandardButton p_button) {
  auto *timer = new QTimer();
  timer->setInterval(50);
  auto *tries = new int(0);
  QObject::connect(timer, &QTimer::timeout, [timer, tries, p_button]() {
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget *w : widgets) {
      auto *box = qobject_cast<QMessageBox *>(w);
      if (box && box->isVisible()) {
        QAbstractButton *btn = box->button(p_button);
        if (btn) {
          btn->click();
          timer->stop();
          delete tries;
          timer->deleteLater();
          return;
        }
      }
    }
    if (++*tries > 120) { // ~6s timeout
      timer->stop();
      delete tries;
      timer->deleteLater();
    }
  });
  timer->start();
  return timer;
}

// =============================================================================
// Subtest 1: empty path -> false
// =============================================================================
void TestViewSplit2ReloadMenu::testEmptyPathReturnsFalse() {
  // Default-constructed Buffer2 has no resolved path.
  vnotex::Buffer2 emptyBuf;
  TestViewWindow2 view(m_services, emptyBuf);
  view.setInitialMode(vnotex::ViewWindowMode::Read);

  QVERIFY(!view.reload());

  // Nothing should have been touched.
  QCOMPARE(view.setModeCallCount, 0);
  QCOMPARE(view.syncEditorCallCount, 0);
  QCOMPARE(view.captureScrollCallCount, 0);
}

// =============================================================================
// Subtest 2: clean reload picks up externally-rewritten disk content
// =============================================================================
void TestViewSplit2ReloadMenu::testCleanReloadPicksUpDiskContent() {
  const QString relPath = QStringLiteral("clean_reload.md");
  const QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  vnotex::Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("original\n")));
  QVERIFY(buf.save());
  QVERIFY(!buf.isModified());

  const QString diskPath = buf.resolvedPath();
  QVERIFY(!diskPath.isEmpty());

  // Externally rewrite the file on disk.
  writeFile(diskPath, QByteArray("updated\n"));

  TestViewWindow2 view(m_services, buf);
  view.setInitialMode(vnotex::ViewWindowMode::Edit);

  const bool ok = view.reload();
  QVERIFY(ok);

  QCOMPARE(view.getBuffer().getContentRaw(), QByteArray("updated\n"));
  QVERIFY(!view.isModified());
  QCOMPARE(view.setModeCallCount, 0);        // Mode is preserved across reload.
  QVERIFY(view.captureScrollCallCount >= 1); // Scroll capture invoked.
  QCOMPARE(view.syncEditorCallCount, 1);     // Editor synced after reload.
  QVERIFY(view.restoreScrollCallCount >= 1); // Scroll restored after reload.
}

// =============================================================================
// Subtest 3: dirty + Cancel keeps state
// =============================================================================
void TestViewSplit2ReloadMenu::testDirtyCancelKeepsState() {
  const QString relPath = QStringLiteral("dirty_cancel.md");
  const QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  vnotex::Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("original\n")));
  QVERIFY(buf.save());

  TestViewWindow2 view(m_services, buf);
  view.setInitialMode(vnotex::ViewWindowMode::Edit);

  // Mark dirty: edit buffer in-memory without saving.
  QVERIFY(view.getBuffer().setContentRaw(QByteArray("edited\n")));
  QVERIFY(view.isModified());

  // Pre-arm a timer to click Cancel on the next QMessageBox.
  armMessageBoxClick(QMessageBox::Cancel);

  const bool ok = view.reload();
  QVERIFY(!ok);
  QVERIFY(view.isModified()); // Cancel preserves dirty state.
  QCOMPARE(view.getBuffer().getContentRaw(), QByteArray("edited\n"));
  QCOMPARE(readFile(view.getBuffer().resolvedPath()), QByteArray("original\n"));
}

// =============================================================================
// Subtest 4: dirty + Discard replaces with disk content
// =============================================================================
void TestViewSplit2ReloadMenu::testDirtyDiscardReplacesContent() {
  const QString relPath = QStringLiteral("dirty_discard.md");
  const QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  vnotex::Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("original\n")));
  QVERIFY(buf.save());

  const QString diskPath = buf.resolvedPath();
  // Externally rewrite the file on disk to a different value.
  writeFile(diskPath, QByteArray("disk-version\n"));

  TestViewWindow2 view(m_services, buf);
  view.setInitialMode(vnotex::ViewWindowMode::Edit);

  // Mark dirty: edit buffer in-memory.
  QVERIFY(view.getBuffer().setContentRaw(QByteArray("edited\n")));
  QVERIFY(view.isModified());

  // Pre-arm a timer to click Discard.
  armMessageBoxClick(QMessageBox::Discard);

  const bool ok = view.reload();
  QVERIFY(ok);
  QCOMPARE(view.getBuffer().getContentRaw(), QByteArray("disk-version\n"));
  QVERIFY(!view.isModified());
}

// =============================================================================
// Subtest 5: dirty + Save then reloads
// =============================================================================
void TestViewSplit2ReloadMenu::testDirtySaveSavesThenReloads() {
  const QString relPath = QStringLiteral("dirty_save.md");
  const QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  vnotex::Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("original\n")));
  QVERIFY(buf.save());

  TestViewWindow2 view(m_services, buf);
  view.setInitialMode(vnotex::ViewWindowMode::Edit);

  // Edit in-memory: this is the content that Save should persist.
  QVERIFY(view.getBuffer().setContentRaw(QByteArray("saved-content\n")));
  QVERIFY(view.isModified());

  // Pre-arm a timer to click Save.
  armMessageBoxClick(QMessageBox::Save);

  const bool ok = view.reload();
  QVERIFY(ok);
  QVERIFY(!view.isModified());

  // After Save + reload: disk content matches what was in memory at save time;
  // buffer should also reflect this (re-read from disk).
  QCOMPARE(view.getBuffer().getContentRaw(), QByteArray("saved-content\n"));
  const QString diskPath = view.getBuffer().resolvedPath();
  QCOMPARE(readFile(diskPath), QByteArray("saved-content\n"));
}

// =============================================================================
// Subtest 6: mode is preserved across reload (setMode is never called)
// =============================================================================
void TestViewSplit2ReloadMenu::testModePreservedAcrossReload() {
  const QString relPath = QStringLiteral("mode_preserved.md");
  const QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  vnotex::Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("data\n")));
  QVERIFY(buf.save());

  TestViewWindow2 view(m_services, buf);
  view.setInitialMode(vnotex::ViewWindowMode::Edit);
  const auto originalMode = view.currentMode();
  QCOMPARE(originalMode, vnotex::ViewWindowMode::Edit);

  const bool ok = view.reload();
  QVERIFY(ok);

  // Critical invariant: setMode() must NOT be called by reload().
  QCOMPARE(view.setModeCallCount, 0);
  QCOMPARE(view.currentMode(), originalMode);
}

// =============================================================================
// Subtest 8: the captured caret is handed back to restorePositionState()
// =============================================================================
void TestViewSplit2ReloadMenu::testCursorStateHandedBackOnReload() {
  const QString relPath = QStringLiteral("cursor_roundtrip.md");
  const QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  vnotex::Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("line0\nline1\nline2\n")));
  QVERIFY(buf.save());

  writeFile(buf.resolvedPath(), QByteArray("line0\nline1-updated\nline2\n"));

  TestViewWindow2 view(m_services, buf);
  view.setInitialMode(vnotex::ViewWindowMode::Edit);
  view.fakeCursorLine = 1;
  view.fakeCursorOffset = 3;

  QVERIFY(view.reload());

  QCOMPARE(view.captureScrollCallCount, 1);
  QCOMPARE(view.restoreScrollCallCount, 1);
  QCOMPARE(view.restoredCursorLine, 1);
  QCOMPARE(view.restoredCursorOffset, 3);
}

// =============================================================================
// Subtest 9: a failed reload restores nothing (editor content is untouched)
// =============================================================================
void TestViewSplit2ReloadMenu::testCursorRestoreSkippedWhenReloadFails() {
  const QString relPath = QStringLiteral("cursor_reload_fail.md");
  const QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  vnotex::Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("data\n")));
  QVERIFY(buf.save());

  const QString diskPath = buf.resolvedPath();
  QVERIFY(!diskPath.isEmpty());

  TestViewWindow2 view(m_services, buf);
  view.setInitialMode(vnotex::ViewWindowMode::Edit);
  view.fakeCursorLine = 0;
  view.fakeCursorOffset = 2;

  // Invalidate the backing file; the resolved path stays non-empty so reload()
  // gets past its guard and fails inside Buffer2::reload().
  QVERIFY(QFile::remove(diskPath));

  QVERIFY(!view.reload());

  QCOMPARE(view.syncEditorCallCount, 0);
  QCOMPARE(view.restoreScrollCallCount, 0);
  QCOMPARE(view.restoredCursorLine, -1);
  QCOMPARE(view.restoredCursorOffset, -1);
}

} // namespace tests

QTEST_MAIN(tests::TestViewSplit2ReloadMenu)
#include "test_viewsplit2_reload_menu.moc"
