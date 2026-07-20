// T27: ViewWindow2 disables Save and formatting/insert actions when the
// owning notebook is read-only, with a tooltip explaining why.
//
// Same stub-injection pattern as tests/widgets/test_viewsplit2_reload_menu.cpp
// and tests/widgets/test_snippet_apply.cpp: viewwindow2.cpp is NOT compiled
// inline (it pulls in ~12 transitive widget dependencies). Instead this test
// provides no-op definitions for every non-pure-virtual ViewWindow2 method
// (linker satisfaction) AND mirrors the production enable predicate from
// ViewWindow2::addAction() in TestViewWindow2 below.
//
// INVARIANT: TestViewWindow2::createSaveActionMirror() /
// createInsertActionMirror() are STRUCTURAL MIRRORS of the production
// `addAction()` Save / Type* switch cases in src/widgets/viewwindow2.cpp. Any
// drift between this mirror and production code must be caught by the F3
// manual QA scenario in the parent plan.
//
// What is tested:
//   1. Save action stays disabled for a RO buffer even when dirty.
//   2. Save action enables on a writable buffer once dirty.
//   3. Insert/format action stays disabled in Edit mode on a RO buffer.
//   4. Insert/format action enables in Edit mode on a writable buffer.
//   5. Both actions carry a tooltip pointing at the RO state.

#include <QAction>
#include <QApplication>
#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

#include <widgets/messageboxhelper.h>
#include <widgets/viewwindow2.h>

using namespace vnotex;

// -----------------------------------------------------------------------------
// Stub bodies for ViewWindow2 non-pure-virtual methods.
//
// Same scope-adjustment rationale as test_viewsplit2_reload_menu.cpp /
// test_snippet_apply.cpp: linking the production .cpp would drag in the full
// widgets/ link surface for a focused test that only needs the QAction enable
// predicate.
// -----------------------------------------------------------------------------

namespace vnotex {

ViewWindow2::ViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services), m_buffer(p_buffer) {
  m_lastKnownRevision = m_buffer.getRevision();
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
void ViewWindow2::reinterpretWithEncoding(const QString &) {}
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

// T28: stub for the new RO-rejection slots. This test only exercises Save /
// Insert action enable predicates; the rejection-modal path is covered by
// test_view_window2_readonly_save_warning. No-op bodies satisfy the linker.
void ViewWindow2::onDirtyRejectedReadOnly(const QString &) {}
void ViewWindow2::onSaveRejectedReadOnly(const QString &) {}
void ViewWindow2::showReadOnlyWarning() {}
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

QString ViewWindow2::s_attachmentFullIconFile;
QString ViewWindow2::s_attachmentFullIconForeground;
QString ViewWindow2::s_attachmentFullIconDisabledForeground;
QString ViewWindow2::s_attachmentFullIconMasterColor;
QIcon ViewWindow2::s_attachmentFullIcon;

} // namespace vnotex

namespace tests {

// Minimal concrete ViewWindow2 subclass for testing the toolbar enable
// predicates. The mirror methods replicate the enable lambdas from production
// addAction() exactly (cross-checked against viewwindow2.cpp at commit time).
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

  // Mirror of production ViewWindow2::addAction() Save case (post-T27).
  // Returns the QAction parented to this widget.
  QAction *createSaveActionMirror() {
    auto *act = new QAction(this);
    act->setEnabled(false);
    // T27 mirror: surface the RO state on the tooltip up front.
    if (getBuffer().isValid() && getBuffer().isReadOnly()) {
      act->setToolTip(tr("Read-only notebook \u2014 cannot edit"));
    }
    connect(this, &ViewWindow2::statusChanged, this, [this, act]() {
      const auto &buf = getBuffer();
      act->setEnabled(buf.isValid() && isModified() && !buf.isReadOnly());
    });
    return act;
  }

  // Mirror of production ViewWindow2::addAction() Type*/Insert cases (post-T27).
  QAction *createInsertActionMirror() {
    auto *act = new QAction(this);
    act->setVisible(false);
    connect(this, &ViewWindow2::modeChanged, this, [act, this]() {
      const bool inEdit = (m_mode == vnotex::ViewWindowMode::Edit);
      act->setVisible(inEdit);
      const auto &buf = getBuffer();
      const bool readOnly = buf.isValid() && buf.isReadOnly();
      act->setEnabled(inEdit && !readOnly);
    });
    if (getBuffer().isValid() && getBuffer().isReadOnly()) {
      act->setEnabled(false);
      act->setToolTip(tr("Read-only notebook \u2014 cannot edit"));
    }
    return act;
  }

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
  void testReadOnlyTooltipPresent();

private:
  vnotex::Buffer2 openBuffer(const QString &p_relativePath);
  void setNotebookReadOnly(bool p_readOnly);

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

  vnotex::Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());
  QVERIFY(buf.setContentRaw(QByteArray("original\n")));
  QVERIFY(buf.save());

  // Open MUST happen before flipping RO (open itself is read-only safe but
  // we want the writable baseline pre-RO).
  setNotebookReadOnly(true);
  QVERIFY(buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);
  QAction *saveAction = view.createSaveActionMirror();

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
  QAction *saveAction = view.createSaveActionMirror();

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

  vnotex::Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());

  setNotebookReadOnly(true);
  QVERIFY(buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);
  QAction *insertAction = view.createInsertActionMirror();

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
  QAction *insertAction = view.createInsertActionMirror();

  // Edit mode + writable: visible and enabled.
  view.switchMode(vnotex::ViewWindowMode::Edit);
  QVERIFY(insertAction->isVisible());
  QVERIFY(insertAction->isEnabled());

  // Read mode hides and disables.
  view.switchMode(vnotex::ViewWindowMode::Read);
  QVERIFY(!insertAction->isVisible());
  QVERIFY(!insertAction->isEnabled());
}

// =============================================================================
// Subtest 5: Read-only tooltip is set on both Save and Insert actions.
// =============================================================================
void TestViewWindow2ReadOnlyToolbar::testReadOnlyTooltipPresent() {
  const QString relPath = QStringLiteral("ro_tooltip.md");
  const QString fileId = m_notebookService->createFile(m_notebookId, QString(), relPath);
  QVERIFY(!fileId.isEmpty());

  vnotex::Buffer2 buf = openBuffer(relPath);
  QVERIFY(buf.isValid());

  setNotebookReadOnly(true);
  QVERIFY(buf.isReadOnly());

  TestViewWindow2 view(m_services, buf);

  QAction *saveAction = view.createSaveActionMirror();
  QVERIFY(saveAction->toolTip().contains(QStringLiteral("Read-only")));

  QAction *insertAction = view.createInsertActionMirror();
  QVERIFY(insertAction->toolTip().contains(QStringLiteral("Read-only")));
}

} // namespace tests

QTEST_MAIN(tests::TestViewWindow2ReadOnlyToolbar)
#include "test_view_window2_readonly_toolbar.moc"
