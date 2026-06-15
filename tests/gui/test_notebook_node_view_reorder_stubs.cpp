// test_notebook_node_view_reorder_stubs.cpp
//
// Stub bodies for NotebookNodeController methods that the GUI drag-reorder
// test references via the view's dispatch path but does NOT exercise in any
// meaningful way. Required because:
//   1. moc-generated qt_metacall references EVERY slot in its dispatch table
//      even when the test never invokes the dispatch path
//   2. The view's dragMoveEvent / dropEvent reference openNode, copyNodes,
//      pasteNodes, isSingleClickActivationEnabled, handleImportFiles, and the
//      base-class virtual moveNodes
//   3. getParentFolder must compute correctly (it is consulted by the
//      dispatch code path under test) so we replicate the real impl verbatim
//
// This file is compiled ONLY into the test target — production builds use
// the real implementations from notebooknodecontroller.cpp.

#include <QString>
#include <QStringList>

#include <controllers/notebooknodecontroller.h>

namespace vnotex {

// Real implementation (must compute correctly — the dispatch logic in
// NotebookNodeView relies on this to determine same-parent vs cross-parent).
NodeIdentifier NotebookNodeController::getParentFolder(const NodeIdentifier &p_nodeId) const {
  NodeIdentifier parentId;
  parentId.notebookId = p_nodeId.notebookId;
  parentId.relativePath = p_nodeId.parentPath();
  return parentId;
}

// Base virtual moveNodes — overridden by RecordingController in the test.
// The linker still requires a definition for the vtable. The body is a no-op
// because no test path invokes the base impl (every test instance is a
// RecordingController whose vtable points to its own override).
void NotebookNodeController::moveNodes(const QList<NodeIdentifier> &, const NodeIdentifier &) {}

// View-referenced methods the test never invokes (Ctrl-drag copy path,
// double-click activation, single-click activation, URL drop import).
void NotebookNodeController::openNode(const NodeIdentifier &) {}
void NotebookNodeController::copyNodes(const QList<NodeIdentifier> &) {}
void NotebookNodeController::pasteNodes(const NodeIdentifier &) {}
bool NotebookNodeController::isSingleClickActivationEnabled() const { return false; }

// moc qt_metacall dispatch entries (public slots) — minimum surface needed
// for the meta-object to link. Bodies are no-ops; tests never go through
// the meta-call path for these.
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

// Read-only gating helpers (read-only feature) — writable in tests.
bool NotebookNodeController::isNotebookReadOnly(const QString &) const { return false; }
bool NotebookNodeController::isSelectionReadOnly(const QList<NodeIdentifier> &) const {
  return false;
}

} // namespace vnotex
