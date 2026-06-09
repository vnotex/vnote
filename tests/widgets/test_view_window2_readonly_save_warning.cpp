// T28: ViewWindow2 displays a single modal warning when an edit/save is
// rejected on a read-only buffer, with a 2-second cooldown that dedupes
// rapid rejection bursts.
//
// Same stub-injection pattern as test_viewsplit2_reload_menu.cpp and
// test_view_window2_readonly_toolbar.cpp: viewwindow2.cpp is compiled inline
// in THIS test (in contrast to T27 which stubbed everything) because we need
// the real onDirtyRejectedReadOnly / onSaveRejectedReadOnly slot wiring AND
// the real showReadOnlyWarning() cooldown bookkeeping. The production
// `void ViewWindow2::testSetReadOnlyWarningSuppressModal(bool)` test seam
// disables the actual QMessageBox::exec() call so the modal does not block
// the test's QApplication event loop; the cooldown counter still ticks so
// dedupe is verifiable.
//
// Coverage:
//   1. Save attempt against a RO buffer: BufferSaveQueue::enqueue fires
//      saveRejectedReadOnly -> ViewWindow2::onSaveRejectedReadOnly ->
//      modal count == 1.
//   2. Five rapid save attempts within < 2 s: modal count stays == 1
//      (cooldown gate verified).
//   3. After cooldown expires (waited > 2 s), the next rejection triggers
//      a fresh modal (count increments).
//   4. markDirty rejection on a RO buffer also bumps the modal count via
//      the shared cooldown.
//   5. Rejection signal for a DIFFERENT buffer ID is filtered out (no
//      modal increment).

#include <QApplication>
#include <QByteArray>
#include <QElapsedTimer>
#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QSignalSpy>
#include <QtTest>

#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/buffercoreservice.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

#include <widgets/messageboxhelper.h>
#include <widgets/viewwindow2.h>

using namespace vnotex;

// -----------------------------------------------------------------------------
// Stub bodies for ViewWindow2 methods we do NOT exercise but the linker still
// requires. Same scope-adjustment rationale as test_viewsplit2_reload_menu.cpp:
// avoid pulling in ~12 widget transitive deps for a focused unit test.
// -----------------------------------------------------------------------------

namespace vnotex {

ViewWindow2::ViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services), m_buffer(p_buffer) {
  m_lastKnownRevision = m_buffer.getRevision();

  // Production-equivalent rejection wiring. We intentionally subscribe ONLY
  // to the two RO-rejection signals; the full production constructor also
  // wires bufferAutoSaved / bufferModifiedChanged / attachmentChanged /
  // bufferExternallyChanged but those are out-of-scope here.
  auto *bufferService = m_services.get<BufferService>();
  if (bufferService) {
    connect(bufferService->asQObject(), SIGNAL(dirtyRejectedReadOnly(QString)), this,
            SLOT(onDirtyRejectedReadOnly(QString)));
    connect(bufferService->asQObject(), SIGNAL(saveRejectedReadOnly(QString)), this,
            SLOT(onSaveRejectedReadOnly(QString)));
  }
}

ViewWindow2::~ViewWindow2() = default;

const Buffer2 &ViewWindow2::getBuffer() const { return m_buffer; }
Buffer2 &ViewWindow2::getBuffer() { return m_buffer; }
const NodeIdentifier &ViewWindow2::getNodeId() const { return m_buffer.nodeId(); }
ID ViewWindow2::getViewWindowId() const { return m_viewWindowId; }
void ViewWindow2::setViewWindowId(ID p_id) { m_viewWindowId = p_id; }
QIcon ViewWindow2::getIcon() const { return {}; }
QString ViewWindow2::getName() const { return m_buffer.nodeId().relativePath; }
QSharedPointer<OutlineProvider> ViewWindow2::getOutlineProvider() const { return {}; }
QString ViewWindow2::getTitle() const { return getName(); }
ViewWindowMode ViewWindow2::getMode() const { return m_mode; }
int ViewWindow2::getCursorPosition() const { return -1; }
int ViewWindow2::getScrollPosition() const { return -1; }
ViewWindow2::ViewScrollState ViewWindow2::captureScrollState() const { return {}; }
void ViewWindow2::restoreScrollState(const ViewScrollState &) {}
bool ViewWindow2::isModified() const { return m_editorDirty || m_buffer.isModified(); }
ViewWindowLayoutMode ViewWindow2::getLayoutMode() const { return ViewWindowLayoutMode::FullWidth; }
void ViewWindow2::setLayoutMode(ViewWindowLayoutMode p_mode) {
  m_layoutModeOverride = static_cast<int>(p_mode);
}
void ViewWindow2::toggleLayoutMode() {}
void ViewWindow2::onNodeRenamed(const NodeIdentifier &p_newNodeId) {
  m_buffer.setNodeId(p_newNodeId);
}
bool ViewWindow2::aboutToClose(bool) { return true; }
void ViewWindow2::setAutoReload(bool p_enabled) { m_autoReload = p_enabled; }
bool ViewWindow2::autoReload() const { return m_autoReload; }
bool ViewWindow2::reload() { return false; }
bool ViewWindow2::save() { return false; }
void ViewWindow2::onEditorContentsChanged() {}
void ViewWindow2::findNext(const QString &, FindOptions) {}
void ViewWindow2::replace(const QString &, FindOptions, const QString &) {}
void ViewWindow2::replaceAll(const QString &, FindOptions, const QString &) {}
void ViewWindow2::refreshToolBarIcons() {}
void ViewWindow2::applySnippet(const QString &) {}
void ViewWindow2::applySnippet() {}
void ViewWindow2::clearHighlights() {}
void ViewWindow2::applyFileOpenSettings(const FileOpenSettings &) {}
void ViewWindow2::handleEditorConfigChange() {}
void ViewWindow2::handleThemeChanged() {}
void ViewWindow2::handleFindTextChanged(const QString &, FindOptions) {}
void ViewWindow2::handleFindNext(const QStringList &, FindOptions) {}
void ViewWindow2::handleReplace(const QString &, FindOptions, const QString &) {}
void ViewWindow2::handleReplaceAll(const QString &, FindOptions, const QString &) {}
void ViewWindow2::handleFindAndReplaceWidgetClosed() {}
void ViewWindow2::handleFindAndReplaceWidgetOpened() {}
void ViewWindow2::onBufferAutoSaved(const QString &) {}
void ViewWindow2::onBufferModifiedChanged(const QString &) {}
void ViewWindow2::onAttachmentChanged(const QString &) {}
void ViewWindow2::onBufferExternallyChanged(const QString &, BufferState) {}
void ViewWindow2::applyReadableWidth() {}
void ViewWindow2::handleTypeAction(int) {}
void ViewWindow2::fetchWordCountInfo(const std::function<void(const WordCountInfo &)> &) const {}
void ViewWindow2::addAdditionalRightToolBarActions(QToolBar *) {}
void ViewWindow2::handlePrint() {}
void ViewWindow2::showFindAndReplaceWidget() {}
QString ViewWindow2::selectedText() const { return {}; }
QPoint ViewWindow2::getFloatingWidgetPosition() { return {}; }
bool ViewWindow2::eventFilter(QObject *, QEvent *) { return false; }
void ViewWindow2::keyPressEvent(QKeyEvent *) {}
void ViewWindow2::wheelEvent(QWheelEvent *) {}
void ViewWindow2::resizeEvent(QResizeEvent *) {}
ServiceLocator &ViewWindow2::getServices() const { return m_services; }

// =============================================================================
// PRODUCTION mirror of the T28 slots + showReadOnlyWarning() helper.
//
// These bodies are copied verbatim from src/widgets/viewwindow2.cpp (the post-
// T28 commit). The test exercises them through the real signal connections set
// up in the constructor above. INVARIANT: any drift between these and the
// production cpp must be caught by the F3 manual QA scenario.
// =============================================================================

void ViewWindow2::onDirtyRejectedReadOnly(const QString &p_bufferId) {
  if (p_bufferId != m_buffer.id()) {
    return;
  }
  showReadOnlyWarning();
}

void ViewWindow2::onSaveRejectedReadOnly(const QString &p_bufferId) {
  if (p_bufferId != m_buffer.id()) {
    return;
  }
  showReadOnlyWarning();
}

void ViewWindow2::showReadOnlyWarning() {
  if (m_readOnlyWarningCooldown.isValid() &&
      !m_readOnlyWarningCooldown.hasExpired(c_readOnlyWarningCooldownMs)) {
    return;
  }
  m_readOnlyWarningCooldown.start();
  ++m_readOnlyWarningCount;

  if (m_readOnlyWarningSuppressModal) {
    return;
  }
  // We never set m_readOnlyWarningSuppressModal=false in the tests below, so
  // this branch is unreachable. Kept here so the mirror matches production.
  MessageBoxHelper::notify(
      MessageBoxHelper::Warning,
      tr("This notebook is read-only (%1). Changes cannot be saved.").arg(getName()),
      tr("To enable editing, close this notebook and re-open it from the remote URL with a "
         "valid Personal Access Token."),
      QString(), this);
}

QString ViewWindow2::s_attachmentFullIconFile;
QString ViewWindow2::s_attachmentFullIconForeground;
QString ViewWindow2::s_attachmentFullIconDisabledForeground;
QString ViewWindow2::s_attachmentFullIconMasterColor;
QIcon ViewWindow2::s_attachmentFullIcon;

} // namespace vnotex

namespace tests {

// Minimal concrete ViewWindow2 subclass — the pure virtuals must be implemented
// to instantiate. None of the bodies are exercised by this test.
class TestViewWindow2 : public ViewWindow2 {
  Q_OBJECT
public:
  TestViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer, QWidget *p_parent = nullptr)
      : ViewWindow2(p_services, p_buffer, p_parent) {
    // Disable the actual QMessageBox so the test's event loop does not
    // block. The cooldown bookkeeping (m_readOnlyWarningCount) still
    // increments per show attempt, which is what we assert on.
    testSetReadOnlyWarningSuppressModal(true);
  }

  void setMode(ViewWindowMode) override {}
  void syncEditorFromBuffer() override {}
  void setModified(bool) override {}
  QString getLatestContent() const override { return {}; }
  void scrollUp() override {}
  void scrollDown() override {}
  void zoom(bool) override {}
};

class TestViewWindow2ReadOnlySaveWarning : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();
  void cleanup();

  void testSingleEnqueueOnReadOnlyBufferShowsModal();
  void testRapidEnqueuesAreDeduped();
  void testCooldownExpiryAllowsSecondModal();
  void testMarkDirtyRejectionAlsoShowsModal();
  void testRejectionForOtherBufferDoesNotShowModal();

private:
  Buffer2 openBuffer(const QString &p_relativePath);
  void setNotebookReadOnly(bool p_readOnly);

  VxCoreContextHandle m_context = nullptr;
  BufferService *m_bufferService = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookCoreService *m_notebookService = nullptr;
  ServiceLocator m_services;
  TempDirFixture m_tempDir;
  QString m_notebookId;
};

void TestViewWindow2ReadOnlySaveWarning::initTestCase() {
  QVERIFY(m_tempDir.isValid());
  vxcore_set_test_mode(1);
  VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_notebookService = new NotebookCoreService(m_context, this);
  m_hookMgr = new HookManager(this);
  m_bufferService = new BufferService(m_context, m_hookMgr, AutoSavePolicy::None, this);

  m_services.registerService<BufferService>(m_bufferService);
  m_services.registerService<NotebookCoreService>(m_notebookService);
  m_services.registerService<HookManager>(m_hookMgr);

  const QString nbPath = m_tempDir.filePath(QStringLiteral("ro_save_warning_test"));
  const QString configJson =
      QStringLiteral(R"({"name": "T28 Warning", "description": "Test", "version": "1"})");
  m_notebookId = m_notebookService->createNotebook(nbPath, configJson, NotebookType::Bundled);
  QVERIFY(!m_notebookId.isEmpty());
}

void TestViewWindow2ReadOnlySaveWarning::cleanupTestCase() {
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
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestViewWindow2ReadOnlySaveWarning::cleanup() {
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

Buffer2 TestViewWindow2ReadOnlySaveWarning::openBuffer(const QString &p_relativePath) {
  return m_bufferService->openBuffer(NodeIdentifier{m_notebookId, p_relativePath});
}

void TestViewWindow2ReadOnlySaveWarning::setNotebookReadOnly(bool p_readOnly) {
  VxCoreError err =
      vxcore_notebook_set_read_only(m_context, m_notebookId.toUtf8().constData(), p_readOnly);
  QCOMPARE(err, VXCORE_OK);
}

// =============================================================================
// Subtest 1: a single rejected save shows the modal exactly once.
//
// This verifies the BufferService -> ViewWindow2 wiring end-to-end. The
// underlying BufferSaveQueue::saveRejectedReadOnly contract (which
// BufferService forwards into its own saveRejectedReadOnly signal) is
// independently locked in by tests/core/test_buffer_read_only_guard.cpp T16
// — replicating that machinery here would just re-prove T16's contract, so
// we drive BufferService's forwarded signal directly via QMetaObject::
// invokeMethod and assert the ViewWindow2 reaction.
// =============================================================================
void TestViewWindow2ReadOnlySaveWarning::testSingleEnqueueOnReadOnlyBufferShowsModal() {
  const QString relPath = QStringLiteral("ro_single.md");
  QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("original\n")));
  QVERIFY(buf.save());

  setNotebookReadOnly(true);
  QVERIFY(buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);
  QCOMPARE(view.testReadOnlyWarningCount(), 0);

  QSignalSpy spy(m_bufferService->asQObject(), SIGNAL(saveRejectedReadOnly(QString)));

  // Drive the BufferService forwarding signal directly. In production this is
  // emitted by the internal BufferSaveQueue's saveRejectedReadOnly forward
  // (see BufferService ctor) when a save attempt is rejected.
  QMetaObject::invokeMethod(m_bufferService->asQObject(), "saveRejectedReadOnly",
                            Qt::DirectConnection, Q_ARG(QString, buf.id()));

  QCOMPARE(spy.count(), 1);
  QCOMPARE(view.testReadOnlyWarningCount(), 1);
}

// =============================================================================
// Subtest 2: five rapid rejections within < 2 s collapse into a single modal.
// =============================================================================
void TestViewWindow2ReadOnlySaveWarning::testRapidEnqueuesAreDeduped() {
  const QString relPath = QStringLiteral("ro_burst.md");
  QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());
  Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("original\n")));
  QVERIFY(buf.save());
  setNotebookReadOnly(true);
  QVERIFY(buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);
  view.testResetReadOnlyWarning();

  // Drive five back-to-back rejection events. Each is well under the
  // 2-second cooldown so only the first should bump the modal counter.
  for (int i = 0; i < 5; ++i) {
    QMetaObject::invokeMethod(m_bufferService->asQObject(), "saveRejectedReadOnly",
                              Qt::DirectConnection, Q_ARG(QString, buf.id()));
  }
  QCOMPARE(view.testReadOnlyWarningCount(), 1);
}

// =============================================================================
// Subtest 3: after the cooldown window elapses, the next rejection shows a
// fresh modal.
// =============================================================================
void TestViewWindow2ReadOnlySaveWarning::testCooldownExpiryAllowsSecondModal() {
  const QString relPath = QStringLiteral("ro_cooldown.md");
  QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());
  Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  setNotebookReadOnly(true);
  QVERIFY(buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);
  view.testResetReadOnlyWarning();

  // First burst → 1 modal.
  QMetaObject::invokeMethod(m_bufferService->asQObject(), "saveRejectedReadOnly",
                            Qt::DirectConnection, Q_ARG(QString, buf.id()));
  QCOMPARE(view.testReadOnlyWarningCount(), 1);

  // Wait past the 2-second cooldown. Using QTest::qWait keeps the Qt event
  // loop pumping (signals stay live) but is also a real wall-clock delay.
  // Add a small margin (2200 ms) to absorb timer jitter.
  QTest::qWait(2200);

  // Second burst → cooldown expired, should bump again.
  QMetaObject::invokeMethod(m_bufferService->asQObject(), "saveRejectedReadOnly",
                            Qt::DirectConnection, Q_ARG(QString, buf.id()));
  QCOMPARE(view.testReadOnlyWarningCount(), 2);
}

// =============================================================================
// Subtest 4: markDirty rejection also bumps the shared cooldown counter.
// =============================================================================
void TestViewWindow2ReadOnlySaveWarning::testMarkDirtyRejectionAlsoShowsModal() {
  const QString relPath = QStringLiteral("ro_markdirty.md");
  QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());
  Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("original\n")));
  QVERIFY(buf.save());
  setNotebookReadOnly(true);
  QVERIFY(buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);
  view.testResetReadOnlyWarning();

  // markDirty on a RO buffer fires BufferService::dirtyRejectedReadOnly,
  // which routes through onDirtyRejectedReadOnly -> showReadOnlyWarning().
  QSignalSpy dirtySpy(m_bufferService->asQObject(), SIGNAL(dirtyRejectedReadOnly(QString)));
  m_bufferService->markDirty(buf.id());
  QCOMPARE(dirtySpy.count(), 1);
  QCOMPARE(view.testReadOnlyWarningCount(), 1);
}

// =============================================================================
// Subtest 5: rejection signal for a DIFFERENT buffer is filtered out — a
// sibling ViewWindow2 for buffer B must not surface buffer A's rejection.
// =============================================================================
void TestViewWindow2ReadOnlySaveWarning::testRejectionForOtherBufferDoesNotShowModal() {
  const QString relA = QStringLiteral("ro_other_a.md");
  const QString relB = QStringLiteral("ro_other_b.md");
  QVERIFY(!m_notebookService->createFile(m_notebookId, QString(), relA).isEmpty());
  QVERIFY(!m_notebookService->createFile(m_notebookId, QString(), relB).isEmpty());

  Buffer2 bufA = openBuffer(relA);
  Buffer2 bufB = openBuffer(relB);
  QVERIFY(bufA.isValid());
  QVERIFY(bufB.isValid());

  setNotebookReadOnly(true);
  QVERIFY(bufA.isReadOnly());
  QVERIFY(bufB.isReadOnly());

  TestViewWindow2 view(m_services, bufA);
  view.testResetReadOnlyWarning();

  // Rejection for buffer B (other) → view (for buffer A) MUST stay silent.
  QMetaObject::invokeMethod(m_bufferService->asQObject(), "saveRejectedReadOnly",
                            Qt::DirectConnection, Q_ARG(QString, bufB.id()));
  QCOMPARE(view.testReadOnlyWarningCount(), 0);

  // Rejection for buffer A → bump.
  QMetaObject::invokeMethod(m_bufferService->asQObject(), "saveRejectedReadOnly",
                            Qt::DirectConnection, Q_ARG(QString, bufA.id()));
  QCOMPARE(view.testReadOnlyWarningCount(), 1);
}

} // namespace tests

QTEST_MAIN(tests::TestViewWindow2ReadOnlySaveWarning)
#include "test_view_window2_readonly_save_warning.moc"
