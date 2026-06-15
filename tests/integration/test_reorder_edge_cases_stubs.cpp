// test_reorder_edge_cases_stubs.cpp
//
// Stub bodies for NotebookNodeController methods that the integration
// edge-case test references via the view dispatch path but does NOT
// exercise. Mirrors tests/gui/test_notebook_node_view_reorder_stubs.cpp so
// the small-TU controller link strategy still produces a complete vtable.
//
// Required because:
//   1. moc-generated qt_metacall references every slot in the dispatch table
//   2. NotebookNodeView's drag/drop path consults openNode, copyNodes,
//      pasteNodes, isSingleClickActivationEnabled, handleImportFiles, and
//      the base-class virtual moveNodes
//   3. getParentFolder must compute correctly — the dispatch logic relies on
//      it to decide same-parent vs cross-parent — so we replicate the real
//      production impl verbatim
//
// This file is compiled ONLY into the test target — production builds use
// the full notebooknodecontroller.cpp.

#include <QString>
#include <QStringList>

#include <controllers/notebooknodecontroller.h>

namespace vnotex {

NodeIdentifier NotebookNodeController::getParentFolder(const NodeIdentifier &p_nodeId) const {
  NodeIdentifier parentId;
  parentId.notebookId = p_nodeId.notebookId;
  parentId.relativePath = p_nodeId.parentPath();
  return parentId;
}

// Base virtual moveNodes — overridden by RecordingController in the test.
// The linker still requires a definition for the vtable.
void NotebookNodeController::moveNodes(const QList<NodeIdentifier> &, const NodeIdentifier &) {}

// View-referenced methods the integration test never invokes (Ctrl-drag copy
// path, double-click activation, single-click activation, URL drop import).
void NotebookNodeController::openNode(const NodeIdentifier &) {}
void NotebookNodeController::copyNodes(const QList<NodeIdentifier> &) {}
void NotebookNodeController::pasteNodes(const NodeIdentifier &) {}
bool NotebookNodeController::isSingleClickActivationEnabled() const { return false; }

// moc qt_metacall dispatch entries (public slots) — minimum surface needed
// for the meta-object to link.
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
