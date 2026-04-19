#include <QtTest>

#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

namespace tests {

class TestHookEvents : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  // Round-trip serialization tests.
  void testNodeOperationEventRoundTrip();
  void testNodeRenameEventRoundTrip();
  void testFileOpenEventRoundTrip();
  void testBufferEventRoundTrip();
  void testViewWindowOpenEventRoundTrip();
  void testViewWindowCloseEventRoundTrip();
  void testViewWindowMoveEventRoundTrip();
  void testViewSplitCreateEventRoundTrip();
  void testViewSplitRemoveEventRoundTrip();
  void testViewSplitActivateEventRoundTrip();
  void testTagOperationEventRoundTrip();
  void testFileTagEventRoundTrip();

  // Theme event round-trip test.
  void testThemeSwitchEventRoundTrip();

  // Attachment event round-trip tests.
  void testAttachmentAddEventRoundTrip();
  void testAttachmentDeleteEventRoundTrip();
  void testAttachmentRenameEventRoundTrip();

  // Attachment typed doAction emission test.
  void testTypedDoActionAttachmentAddEvent();

  // Attachment typed addAction subscription test.
  void testTypedAddActionAttachmentRenameEvent();

  // Attachment typed cancellation test.
  void testTypedCancellationAttachmentDeleteEvent();

  // Typed doAction emission tests.
  void testTypedDoActionNodeOperationEvent();
  void testTypedDoActionFileOpenEvent();
  void testTypedDoActionBufferEvent();
  void testTypedDoActionViewWindowMoveEvent();

  // Typed addAction subscription tests.
  void testTypedAddActionNodeRenameEvent();
  void testTypedAddActionBufferEvent();
  void testTypedAddActionViewWindowCloseEvent();

  // NotebookCloseEvent round-trip and typed tests.
  void testNotebookCloseEventRoundTrip();
  void testTypedDoActionNotebookCloseEvent();

  // Cancellation via typed API.
  void testTypedCancellation();

private:
  HookManager *m_hookMgr = nullptr;
};

void TestHookEvents::initTestCase() {
  m_hookMgr = new HookManager(this);
  QVERIFY(m_hookMgr != nullptr);
}

void TestHookEvents::cleanupTestCase() {
  delete m_hookMgr;
  m_hookMgr = nullptr;
}

// ===== Round-trip serialization tests =====

void TestHookEvents::testNodeOperationEventRoundTrip() {
  NodeOperationEvent orig;
  orig.notebookId = QStringLiteral("nb-123");
  orig.relativePath = QStringLiteral("folder/note.md");
  orig.isFolder = false;
  orig.name = QStringLiteral("note.md");
  orig.operation = QStringLiteral("delete");

  QVariantMap map = orig.toVariantMap();
  NodeOperationEvent restored = NodeOperationEvent::fromVariantMap(map);

  QCOMPARE(restored.notebookId, orig.notebookId);
  QCOMPARE(restored.relativePath, orig.relativePath);
  QCOMPARE(restored.isFolder, orig.isFolder);
  QCOMPARE(restored.name, orig.name);
  QCOMPARE(restored.operation, orig.operation);
}

void TestHookEvents::testNodeRenameEventRoundTrip() {
  NodeRenameEvent orig;
  orig.notebookId = QStringLiteral("nb-456");
  orig.relativePath = QStringLiteral("docs/readme.md");
  orig.isFolder = false;
  orig.oldName = QStringLiteral("readme.md");
  orig.newName = QStringLiteral("README.md");

  QVariantMap map = orig.toVariantMap();
  NodeRenameEvent restored = NodeRenameEvent::fromVariantMap(map);

  QCOMPARE(restored.notebookId, orig.notebookId);
  QCOMPARE(restored.relativePath, orig.relativePath);
  QCOMPARE(restored.isFolder, orig.isFolder);
  QCOMPARE(restored.oldName, orig.oldName);
  QCOMPARE(restored.newName, orig.newName);
}

void TestHookEvents::testFileOpenEventRoundTrip() {
  FileOpenEvent orig;
  orig.notebookId = QStringLiteral("nb-789");
  orig.filePath = QStringLiteral("notes/test.md");
  orig.bufferId = QStringLiteral("buf-42");
  orig.mode = 2;
  orig.forceMode = true;
  orig.focus = false;
  orig.newFile = true;
  orig.readOnly = true;
  orig.lineNumber = 55;
  orig.alwaysNewWindow = true;

  QVariantMap map = orig.toVariantMap();
  FileOpenEvent restored = FileOpenEvent::fromVariantMap(map);

  QCOMPARE(restored.notebookId, orig.notebookId);
  QCOMPARE(restored.filePath, orig.filePath);
  QCOMPARE(restored.bufferId, orig.bufferId);
  QCOMPARE(restored.mode, orig.mode);
  QCOMPARE(restored.forceMode, orig.forceMode);
  QCOMPARE(restored.focus, orig.focus);
  QCOMPARE(restored.newFile, orig.newFile);
  QCOMPARE(restored.readOnly, orig.readOnly);
  QCOMPARE(restored.lineNumber, orig.lineNumber);
  QCOMPARE(restored.alwaysNewWindow, orig.alwaysNewWindow);
}

void TestHookEvents::testBufferEventRoundTrip() {
  BufferEvent orig;
  orig.bufferId = QStringLiteral("buf-100");

  QVariantMap map = orig.toVariantMap();
  BufferEvent restored = BufferEvent::fromVariantMap(map);

  QCOMPARE(restored.bufferId, orig.bufferId);
}

void TestHookEvents::testViewWindowOpenEventRoundTrip() {
  ViewWindowOpenEvent orig;
  orig.fileType = QStringLiteral("markdown");
  orig.bufferId = QStringLiteral("buf-200");
  orig.nodeId = QStringLiteral("notes/hello.md");

  QVariantMap map = orig.toVariantMap();
  ViewWindowOpenEvent restored = ViewWindowOpenEvent::fromVariantMap(map);

  QCOMPARE(restored.fileType, orig.fileType);
  QCOMPARE(restored.bufferId, orig.bufferId);
  QCOMPARE(restored.nodeId, orig.nodeId);
}

void TestHookEvents::testViewWindowCloseEventRoundTrip() {
  ViewWindowCloseEvent orig;
  orig.windowId = 12345;
  orig.force = true;
  orig.bufferId = QStringLiteral("buf-300");

  QVariantMap map = orig.toVariantMap();
  ViewWindowCloseEvent restored = ViewWindowCloseEvent::fromVariantMap(map);

  QCOMPARE(restored.windowId, orig.windowId);
  QCOMPARE(restored.force, orig.force);
  QCOMPARE(restored.bufferId, orig.bufferId);
}

void TestHookEvents::testViewWindowMoveEventRoundTrip() {
  ViewWindowMoveEvent orig;
  orig.windowId = 99999;
  orig.srcWorkspaceId = QStringLiteral("ws-1");
  orig.dstWorkspaceId = QStringLiteral("ws-2");
  orig.direction = 3;
  orig.bufferId = QStringLiteral("buf-400");

  QVariantMap map = orig.toVariantMap();
  ViewWindowMoveEvent restored = ViewWindowMoveEvent::fromVariantMap(map);

  QCOMPARE(restored.windowId, orig.windowId);
  QCOMPARE(restored.srcWorkspaceId, orig.srcWorkspaceId);
  QCOMPARE(restored.dstWorkspaceId, orig.dstWorkspaceId);
  QCOMPARE(restored.direction, orig.direction);
  QCOMPARE(restored.bufferId, orig.bufferId);
}

void TestHookEvents::testViewSplitCreateEventRoundTrip() {
  ViewSplitCreateEvent orig;
  orig.workspaceId = QStringLiteral("ws-10");
  orig.direction = 1;
  orig.newWorkspaceId = QStringLiteral("ws-11");

  QVariantMap map = orig.toVariantMap();
  ViewSplitCreateEvent restored = ViewSplitCreateEvent::fromVariantMap(map);

  QCOMPARE(restored.workspaceId, orig.workspaceId);
  QCOMPARE(restored.direction, orig.direction);
  QCOMPARE(restored.newWorkspaceId, orig.newWorkspaceId);
}

void TestHookEvents::testViewSplitRemoveEventRoundTrip() {
  ViewSplitRemoveEvent orig;
  orig.workspaceId = QStringLiteral("ws-20");
  orig.keepWorkspace = true;

  QVariantMap map = orig.toVariantMap();
  ViewSplitRemoveEvent restored = ViewSplitRemoveEvent::fromVariantMap(map);

  QCOMPARE(restored.workspaceId, orig.workspaceId);
  QCOMPARE(restored.keepWorkspace, orig.keepWorkspace);
}

void TestHookEvents::testViewSplitActivateEventRoundTrip() {
  ViewSplitActivateEvent orig;
  orig.workspaceId = QStringLiteral("ws-30");

  QVariantMap map = orig.toVariantMap();
  ViewSplitActivateEvent restored = ViewSplitActivateEvent::fromVariantMap(map);

  QCOMPARE(restored.workspaceId, orig.workspaceId);
}

// ===== Typed doAction emission tests =====
// Register a raw addAction subscriber (QVariantMap callback), emit via typed doAction,
// verify the subscriber received correct args.

void TestHookEvents::testTypedDoActionNodeOperationEvent() {
  bool fired = false;
  QVariantMap captured;
  int hookId = m_hookMgr->addAction(
      HookNames::NodeBeforeDelete,
      [&fired, &captured](HookContext &, const QVariantMap &p_args) {
        fired = true;
        captured = p_args;
      },
      10);

  NodeOperationEvent event;
  event.notebookId = QStringLiteral("nb-emit-1");
  event.relativePath = QStringLiteral("path/to/file.md");
  event.isFolder = false;
  event.name = QStringLiteral("file.md");
  event.operation = QStringLiteral("delete");

  m_hookMgr->doAction(HookNames::NodeBeforeDelete, event);

  QVERIFY(fired);
  QCOMPARE(captured[QStringLiteral("notebookId")].toString(),
           QStringLiteral("nb-emit-1"));
  QCOMPARE(captured[QStringLiteral("relativePath")].toString(),
           QStringLiteral("path/to/file.md"));
  QCOMPARE(captured[QStringLiteral("isFolder")].toBool(), false);
  QCOMPARE(captured[QStringLiteral("name")].toString(), QStringLiteral("file.md"));
  QCOMPARE(captured[QStringLiteral("operation")].toString(),
           QStringLiteral("delete"));

  m_hookMgr->removeAction(hookId);
}

void TestHookEvents::testTypedDoActionFileOpenEvent() {
  bool fired = false;
  QVariantMap captured;
  int hookId = m_hookMgr->addAction(
      HookNames::FileBeforeOpen,
      [&fired, &captured](HookContext &, const QVariantMap &p_args) {
        fired = true;
        captured = p_args;
      },
      10);

  FileOpenEvent event;
  event.notebookId = QStringLiteral("nb-emit-2");
  event.filePath = QStringLiteral("notes/open.md");
  event.bufferId = QStringLiteral("buf-emit-2");
  event.mode = 1;
  event.forceMode = true;
  event.focus = false;
  event.newFile = true;
  event.readOnly = false;
  event.lineNumber = 42;
  event.alwaysNewWindow = true;

  m_hookMgr->doAction(HookNames::FileBeforeOpen, event);

  QVERIFY(fired);
  QCOMPARE(captured[QStringLiteral("notebookId")].toString(),
           QStringLiteral("nb-emit-2"));
  QCOMPARE(captured[QStringLiteral("filePath")].toString(),
           QStringLiteral("notes/open.md"));
  QCOMPARE(captured[QStringLiteral("mode")].toInt(), 1);
  QCOMPARE(captured[QStringLiteral("forceMode")].toBool(), true);
  QCOMPARE(captured[QStringLiteral("lineNumber")].toInt(), 42);
  QCOMPARE(captured[QStringLiteral("alwaysNewWindow")].toBool(), true);

  m_hookMgr->removeAction(hookId);
}

void TestHookEvents::testTypedDoActionBufferEvent() {
  bool fired = false;
  QVariantMap captured;
  int hookId = m_hookMgr->addAction(
      HookNames::FileBeforeSave,
      [&fired, &captured](HookContext &, const QVariantMap &p_args) {
        fired = true;
        captured = p_args;
      },
      10);

  BufferEvent event;
  event.bufferId = QStringLiteral("buf-emit-3");

  m_hookMgr->doAction(HookNames::FileBeforeSave, event);

  QVERIFY(fired);
  QCOMPARE(captured[QStringLiteral("bufferId")].toString(),
           QStringLiteral("buf-emit-3"));

  m_hookMgr->removeAction(hookId);
}

void TestHookEvents::testTypedDoActionViewWindowMoveEvent() {
  bool fired = false;
  QVariantMap captured;
  int hookId = m_hookMgr->addAction(
      HookNames::ViewWindowBeforeMove,
      [&fired, &captured](HookContext &, const QVariantMap &p_args) {
        fired = true;
        captured = p_args;
      },
      10);

  ViewWindowMoveEvent event;
  event.windowId = 77777;
  event.srcWorkspaceId = QStringLiteral("ws-src");
  event.dstWorkspaceId = QStringLiteral("ws-dst");
  event.direction = 2;
  event.bufferId = QStringLiteral("buf-emit-4");

  m_hookMgr->doAction(HookNames::ViewWindowBeforeMove, event);

  QVERIFY(fired);
  QCOMPARE(captured[QStringLiteral("windowId")].toULongLong(), quint64(77777));
  QCOMPARE(captured[QStringLiteral("srcWorkspaceId")].toString(),
           QStringLiteral("ws-src"));
  QCOMPARE(captured[QStringLiteral("dstWorkspaceId")].toString(),
           QStringLiteral("ws-dst"));
  QCOMPARE(captured[QStringLiteral("direction")].toInt(), 2);
  QCOMPARE(captured[QStringLiteral("bufferId")].toString(),
           QStringLiteral("buf-emit-4"));

  m_hookMgr->removeAction(hookId);
}

// ===== Typed addAction subscription tests =====
// Register via typed addAction<EventT>(), emit via raw doAction(hookName, QVariantMap),
// verify the typed callback received correct struct fields.

void TestHookEvents::testTypedAddActionNodeRenameEvent() {
  bool fired = false;
  NodeRenameEvent captured;
  int hookId = m_hookMgr->addAction<NodeRenameEvent>(
      HookNames::NodeAfterRename,
      [&fired, &captured](HookContext &, const NodeRenameEvent &p_event) {
        fired = true;
        captured = p_event;
      },
      10);

  QVariantMap rawArgs;
  rawArgs[QStringLiteral("notebookId")] = QStringLiteral("nb-sub-1");
  rawArgs[QStringLiteral("relativePath")] = QStringLiteral("docs/old.md");
  rawArgs[QStringLiteral("isFolder")] = false;
  rawArgs[QStringLiteral("oldName")] = QStringLiteral("old.md");
  rawArgs[QStringLiteral("newName")] = QStringLiteral("new.md");

  m_hookMgr->doAction(HookNames::NodeAfterRename, rawArgs);

  QVERIFY(fired);
  QCOMPARE(captured.notebookId, QStringLiteral("nb-sub-1"));
  QCOMPARE(captured.relativePath, QStringLiteral("docs/old.md"));
  QCOMPARE(captured.isFolder, false);
  QCOMPARE(captured.oldName, QStringLiteral("old.md"));
  QCOMPARE(captured.newName, QStringLiteral("new.md"));

  m_hookMgr->removeAction(hookId);
}

void TestHookEvents::testTypedAddActionBufferEvent() {
  bool fired = false;
  BufferEvent captured;
  int hookId = m_hookMgr->addAction<BufferEvent>(
      HookNames::FileAfterSave,
      [&fired, &captured](HookContext &, const BufferEvent &p_event) {
        fired = true;
        captured = p_event;
      },
      10);

  QVariantMap rawArgs;
  rawArgs[QStringLiteral("bufferId")] = QStringLiteral("buf-sub-2");

  m_hookMgr->doAction(HookNames::FileAfterSave, rawArgs);

  QVERIFY(fired);
  QCOMPARE(captured.bufferId, QStringLiteral("buf-sub-2"));

  m_hookMgr->removeAction(hookId);
}

void TestHookEvents::testTypedAddActionViewWindowCloseEvent() {
  bool fired = false;
  ViewWindowCloseEvent captured;
  int hookId = m_hookMgr->addAction<ViewWindowCloseEvent>(
      HookNames::ViewWindowBeforeClose,
      [&fired, &captured](HookContext &, const ViewWindowCloseEvent &p_event) {
        fired = true;
        captured = p_event;
      },
      10);

  QVariantMap rawArgs;
  rawArgs[QStringLiteral("windowId")] = quint64(55555);
  rawArgs[QStringLiteral("force")] = true;
  rawArgs[QStringLiteral("bufferId")] = QStringLiteral("buf-sub-3");

  m_hookMgr->doAction(HookNames::ViewWindowBeforeClose, rawArgs);

  QVERIFY(fired);
  QCOMPARE(captured.windowId, quint64(55555));
  QCOMPARE(captured.force, true);
  QCOMPARE(captured.bufferId, QStringLiteral("buf-sub-3"));

  m_hookMgr->removeAction(hookId);
}

// ===== Cancellation via typed API =====

void TestHookEvents::testTypedCancellation() {
  // Register a typed subscriber that cancels the action.
  int hookId = m_hookMgr->addAction<NodeOperationEvent>(
      HookNames::NodeBeforeDelete,
      [](HookContext &p_ctx, const NodeOperationEvent &) { p_ctx.cancel(); }, 10);

  NodeOperationEvent event;
  event.notebookId = QStringLiteral("nb-cancel");
  event.relativePath = QStringLiteral("path/cancel.md");
  event.isFolder = false;
  event.name = QStringLiteral("cancel.md");
  event.operation = QStringLiteral("delete");

  bool cancelled = m_hookMgr->doAction(HookNames::NodeBeforeDelete, event);
  QVERIFY(cancelled);

  m_hookMgr->removeAction(hookId);
}

// ===== Tag event round-trip tests =====

void TestHookEvents::testTagOperationEventRoundTrip() {
  TagOperationEvent orig;
  orig.notebookId = QStringLiteral("nb-tag-1");
  orig.tagName = QStringLiteral("programming");
  orig.parentTag = QStringLiteral("topics");
  orig.operation = QStringLiteral("create");

  QVariantMap map = orig.toVariantMap();
  TagOperationEvent restored = TagOperationEvent::fromVariantMap(map);

  QCOMPARE(restored.notebookId, orig.notebookId);
  QCOMPARE(restored.tagName, orig.tagName);
  QCOMPARE(restored.parentTag, orig.parentTag);
  QCOMPARE(restored.operation, orig.operation);
}

void TestHookEvents::testFileTagEventRoundTrip() {
  FileTagEvent orig;
  orig.notebookId = QStringLiteral("nb-ftag-1");
  orig.filePath = QStringLiteral("notes/hello.md");
  orig.tagName = QStringLiteral("important");
  orig.tagsJson = QStringLiteral("[\"important\",\"todo\"]");
  orig.operation = QStringLiteral("tag");

  QVariantMap map = orig.toVariantMap();
  FileTagEvent restored = FileTagEvent::fromVariantMap(map);

  QCOMPARE(restored.notebookId, orig.notebookId);
  QCOMPARE(restored.filePath, orig.filePath);
  QCOMPARE(restored.tagName, orig.tagName);
  QCOMPARE(restored.tagsJson, orig.tagsJson);
  QCOMPARE(restored.operation, orig.operation);
}

// ===== Theme event round-trip test =====

void TestHookEvents::testThemeSwitchEventRoundTrip() {
  ThemeSwitchEvent orig;
  orig.themeName = QStringLiteral("moonlight");

  QVariantMap map = orig.toVariantMap();
  ThemeSwitchEvent restored = ThemeSwitchEvent::fromVariantMap(map);

  QCOMPARE(restored.themeName, orig.themeName);
}

// ===== Attachment event round-trip tests =====

void TestHookEvents::testAttachmentAddEventRoundTrip() {
  AttachmentAddEvent orig;
  orig.bufferId = QStringLiteral("buf-att-1");
  orig.sourcePath = QStringLiteral("/tmp/image.png");
  orig.filename = QStringLiteral("image.png");

  QVariantMap map = orig.toVariantMap();
  AttachmentAddEvent restored = AttachmentAddEvent::fromVariantMap(map);

  QCOMPARE(restored.bufferId, orig.bufferId);
  QCOMPARE(restored.sourcePath, orig.sourcePath);
  QCOMPARE(restored.filename, orig.filename);
}

void TestHookEvents::testAttachmentDeleteEventRoundTrip() {
  AttachmentDeleteEvent orig;
  orig.bufferId = QStringLiteral("buf-att-2");
  orig.filename = QStringLiteral("old-file.pdf");

  QVariantMap map = orig.toVariantMap();
  AttachmentDeleteEvent restored = AttachmentDeleteEvent::fromVariantMap(map);

  QCOMPARE(restored.bufferId, orig.bufferId);
  QCOMPARE(restored.filename, orig.filename);
}

void TestHookEvents::testAttachmentRenameEventRoundTrip() {
  AttachmentRenameEvent orig;
  orig.bufferId = QStringLiteral("buf-att-3");
  orig.oldFilename = QStringLiteral("old-name.txt");
  orig.newFilename = QStringLiteral("new-name.txt");

  QVariantMap map = orig.toVariantMap();
  AttachmentRenameEvent restored = AttachmentRenameEvent::fromVariantMap(map);

  QCOMPARE(restored.bufferId, orig.bufferId);
  QCOMPARE(restored.oldFilename, orig.oldFilename);
  QCOMPARE(restored.newFilename, orig.newFilename);
}

// ===== Attachment typed doAction emission test =====

void TestHookEvents::testTypedDoActionAttachmentAddEvent() {
  bool fired = false;
  QVariantMap captured;
  int hookId = m_hookMgr->addAction(
      HookNames::AttachmentBeforeAdd,
      [&fired, &captured](HookContext &, const QVariantMap &p_args) {
        fired = true;
        captured = p_args;
      },
      10);

  AttachmentAddEvent event;
  event.bufferId = QStringLiteral("buf-emit-att-1");
  event.sourcePath = QStringLiteral("/home/user/photo.jpg");
  event.filename = QStringLiteral("photo.jpg");

  m_hookMgr->doAction(HookNames::AttachmentBeforeAdd, event);

  QVERIFY(fired);
  QCOMPARE(captured[QStringLiteral("bufferId")].toString(),
           QStringLiteral("buf-emit-att-1"));
  QCOMPARE(captured[QStringLiteral("sourcePath")].toString(),
           QStringLiteral("/home/user/photo.jpg"));
  QCOMPARE(captured[QStringLiteral("filename")].toString(),
           QStringLiteral("photo.jpg"));

  m_hookMgr->removeAction(hookId);
}

// ===== Attachment typed addAction subscription test =====

void TestHookEvents::testTypedAddActionAttachmentRenameEvent() {
  bool fired = false;
  AttachmentRenameEvent captured;
  int hookId = m_hookMgr->addAction<AttachmentRenameEvent>(
      HookNames::AttachmentAfterRename,
      [&fired, &captured](HookContext &, const AttachmentRenameEvent &p_event) {
        fired = true;
        captured = p_event;
      },
      10);

  QVariantMap rawArgs;
  rawArgs[QStringLiteral("bufferId")] = QStringLiteral("buf-sub-att-1");
  rawArgs[QStringLiteral("oldFilename")] = QStringLiteral("before.txt");
  rawArgs[QStringLiteral("newFilename")] = QStringLiteral("after.txt");

  m_hookMgr->doAction(HookNames::AttachmentAfterRename, rawArgs);

  QVERIFY(fired);
  QCOMPARE(captured.bufferId, QStringLiteral("buf-sub-att-1"));
  QCOMPARE(captured.oldFilename, QStringLiteral("before.txt"));
  QCOMPARE(captured.newFilename, QStringLiteral("after.txt"));

  m_hookMgr->removeAction(hookId);
}

// ===== Attachment typed cancellation test =====

void TestHookEvents::testTypedCancellationAttachmentDeleteEvent() {
  int hookId = m_hookMgr->addAction<AttachmentDeleteEvent>(
      HookNames::AttachmentBeforeDelete,
      [](HookContext &p_ctx, const AttachmentDeleteEvent &) { p_ctx.cancel(); }, 10);

  AttachmentDeleteEvent event;
  event.bufferId = QStringLiteral("buf-cancel-att");
  event.filename = QStringLiteral("doomed.pdf");

  bool cancelled = m_hookMgr->doAction(HookNames::AttachmentBeforeDelete, event);
  QVERIFY(cancelled);

  m_hookMgr->removeAction(hookId);
}

// ===== NotebookCloseEvent round-trip test =====

void TestHookEvents::testNotebookCloseEventRoundTrip() {
  NotebookCloseEvent orig;
  orig.notebookId = QStringLiteral("nb-close-1");

  QVariantMap map = orig.toVariantMap();
  NotebookCloseEvent restored = NotebookCloseEvent::fromVariantMap(map);

  QCOMPARE(restored.notebookId, orig.notebookId);
}

void TestHookEvents::testTypedDoActionNotebookCloseEvent() {
  bool fired = false;
  QVariantMap captured;
  int hookId = m_hookMgr->addAction(
      HookNames::NotebookBeforeClose,
      [&fired, &captured](HookContext &, const QVariantMap &p_args) {
        fired = true;
        captured = p_args;
      },
      10);

  NotebookCloseEvent event;
  event.notebookId = QStringLiteral("nb-close-emit-1");

  m_hookMgr->doAction(HookNames::NotebookBeforeClose, event);

  QVERIFY(fired);
  QCOMPARE(captured[QStringLiteral("notebookId")].toString(),
           QStringLiteral("nb-close-emit-1"));

  m_hookMgr->removeAction(hookId);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestHookEvents)
#include "test_hookevents.moc"
