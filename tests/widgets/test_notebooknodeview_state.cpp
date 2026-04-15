// Tests for NotebookNodeView::getExpandedFolders() and replayExpandedFolders().
// We compile notebooknodeview.cpp directly and stub NotebookNodeController
// to satisfy linker references from event handlers we don't exercise.

#include <QtTest>

#include <QStandardItemModel>

#include <views/notebooknodeview.h>

// --- NotebookNodeController stubs (satisfy linker for notebooknodeview.cpp) ---
// The view's event handlers reference controller methods, but our tests never
// trigger those code paths. Provide minimal stubs so the linker is happy.

#include <controllers/notebooknodecontroller.h>
#include <core/servicelocator.h>

namespace vnotex {

NotebookNodeController::NotebookNodeController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}
NotebookNodeController::~NotebookNodeController() = default;

void NotebookNodeController::setModel(INodeListModel *) {}
INodeListModel *NotebookNodeController::model() const { return nullptr; }
void NotebookNodeController::setView(NotebookNodeView *) {}
NotebookNodeView *NotebookNodeController::view() const { return nullptr; }
QMenu *NotebookNodeController::createContextMenu(const NodeIdentifier &, QWidget *) {
  return nullptr;
}
QMenu *NotebookNodeController::createExternalNodeContextMenu(const NodeIdentifier &, QWidget *) {
  return nullptr;
}
void NotebookNodeController::newNote(const NodeIdentifier &) {}
void NotebookNodeController::newFolder(const NodeIdentifier &) {}
void NotebookNodeController::openNode(const NodeIdentifier &) {}
void NotebookNodeController::openNodeWithDefaultApp(const NodeIdentifier &) {}
void NotebookNodeController::deleteNodes(const QList<NodeIdentifier> &) {}
void NotebookNodeController::removeNodesFromNotebook(const QList<NodeIdentifier> &) {}
void NotebookNodeController::copyNodes(const QList<NodeIdentifier> &) {}
void NotebookNodeController::cutNodes(const QList<NodeIdentifier> &) {}
void NotebookNodeController::pasteNodes(const NodeIdentifier &) {}
void NotebookNodeController::duplicateNode(const NodeIdentifier &) {}
void NotebookNodeController::renameNode(const NodeIdentifier &) {}
void NotebookNodeController::markNode(const NodeIdentifier &) {}
void NotebookNodeController::moveNodes(const QList<NodeIdentifier> &, const NodeIdentifier &) {}
void NotebookNodeController::exportNode(const NodeIdentifier &) {}
void NotebookNodeController::importExternalNode(const NodeIdentifier &) {}
void NotebookNodeController::showNodeProperties(const NodeIdentifier &) {}
void NotebookNodeController::copyNodePath(const NodeIdentifier &) {}
void NotebookNodeController::locateNodeInFileManager(const NodeIdentifier &) {}
void NotebookNodeController::sortNodes(const NodeIdentifier &) {}
void NotebookNodeController::reloadNode(const NodeIdentifier &) {}
void NotebookNodeController::reloadAll() {}
void NotebookNodeController::pinNodeToQuickAccess(const NodeIdentifier &) {}
void NotebookNodeController::manageNodeTags(const NodeIdentifier &) {}
bool NotebookNodeController::canPaste() const { return false; }
void NotebookNodeController::shareClipboardWith(NotebookNodeController *) {}
bool NotebookNodeController::isSingleClickActivationEnabled() const { return false; }
void NotebookNodeController::setSelectedNodesCallback(SelectedNodesCallback) {}
void NotebookNodeController::handleNewNoteResult(const NodeIdentifier &, const NodeIdentifier &) {}
void NotebookNodeController::handleNewFolderResult(const NodeIdentifier &, const NodeIdentifier &) {
}
void NotebookNodeController::handleRenameResult(const NodeIdentifier &, const QString &) {}
void NotebookNodeController::handleMarkResult(const NodeIdentifier &, const QString &,
                                              const QString &) {}
void NotebookNodeController::handleDeleteConfirmed(const QList<NodeIdentifier> &, bool) {}
void NotebookNodeController::handleRemoveConfirmed(const QList<NodeIdentifier> &) {}
void NotebookNodeController::handleImportFiles(const NodeIdentifier &, const QStringList &) {}
void NotebookNodeController::handleImportFolder(const NodeIdentifier &, const QString &) {}
NodeIdentifier NotebookNodeController::getParentFolder(const NodeIdentifier &) const {
  return NodeIdentifier();
}

} // namespace vnotex

// --- End stubs ---

using namespace vnotex;

namespace tests {

class TestNotebookNodeViewState : public QObject {
  Q_OBJECT

private slots:
  void testGetExpandedFolders_noModel();
  void testGetExpandedFolders_emptyModel();
  void testReplayExpandedFolders_noModel();
  void testReplayExpandedFolders_emptyList();
  void testReplayExpandedFolders_staleIds();
};

void TestNotebookNodeViewState::testGetExpandedFolders_noModel() {
  // View with no model set — getExpandedFolders returns empty immediately.
  NotebookNodeView view;
  QVERIFY(view.model() == nullptr);

  QList<NodeIdentifier> folders = view.getExpandedFolders();
  QVERIFY(folders.isEmpty());
}

void TestNotebookNodeViewState::testGetExpandedFolders_emptyModel() {
  // View with a model but no items — getExpandedFolders returns empty.
  NotebookNodeView view;
  QStandardItemModel model;
  view.setModel(&model);

  QList<NodeIdentifier> folders = view.getExpandedFolders();
  QVERIFY(folders.isEmpty());
}

void TestNotebookNodeViewState::testReplayExpandedFolders_noModel() {
  // No model — replayExpandedFolders returns immediately without crash.
  NotebookNodeView view;
  QVERIFY(view.model() == nullptr);

  NodeIdentifier staleId;
  staleId.notebookId = QStringLiteral("nb-999");
  staleId.relativePath = QStringLiteral("gone/folder");

  // Should not crash.
  view.replayExpandedFolders({staleId});
}

void TestNotebookNodeViewState::testReplayExpandedFolders_emptyList() {
  // Model set but empty folder list — no-op.
  NotebookNodeView view;
  QStandardItemModel model;
  view.setModel(&model);

  view.replayExpandedFolders({});
  // No crash, no expanded items.
  QVERIFY(view.getExpandedFolders().isEmpty());
}

void TestNotebookNodeViewState::testReplayExpandedFolders_staleIds() {
  // Model set with some items, but replay with IDs that don't match any node.
  // indexFromNodeId will fail qobject_cast to NotebookNodeModel → returns invalid index → skips.
  NotebookNodeView view;
  QStandardItemModel model;

  // Add a dummy item so the model isn't empty.
  auto *item = new QStandardItem(QStringLiteral("folder1"));
  model.appendRow(item);
  view.setModel(&model);

  NodeIdentifier staleId;
  staleId.notebookId = QStringLiteral("nb-999");
  staleId.relativePath = QStringLiteral("nonexistent/folder");

  // Should skip gracefully — no crash, no expansion.
  view.replayExpandedFolders({staleId});

  // The dummy item should NOT be expanded (stale ID doesn't match).
  QVERIFY(!view.isExpanded(model.index(0, 0)));
  QVERIFY(view.getExpandedFolders().isEmpty());
}

} // namespace tests

QTEST_MAIN(tests::TestNotebookNodeViewState)
#include "test_notebooknodeview_state.moc"
