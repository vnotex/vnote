// test_notebook_node_controller_reorder_stubs.cpp
//
// Stub bodies for NotebookNodeController public slots that the test does NOT
// exercise. Required because moc-generated qt_metacall references EVERY slot
// in its dispatch table even when the test never invokes the dispatch path.
// Providing weak empty bodies here lets the test executable link without
// pulling in the slots' real implementations (which transitively need
// BufferService, ConfigMgr2, NotebookNodeModel, etc.).
//
// This file is compiled ONLY into the test target — production builds use the
// real implementations from notebooknodecontroller.cpp. There is therefore no
// duplicate-symbol risk.

#include <QString>
#include <QStringList>

#include <controllers/notebooknodecontroller.h>

namespace vnotex {

// T9 (notebook-explorer-drag-reorder): moveNodes is now virtual on
// NotebookNodeController so the GUI drag-drop test can intercept it. The
// linker therefore requires a definition for the vtable even though this
// test never touches the move path. No-op body is sufficient.
void NotebookNodeController::moveNodes(const QList<NodeIdentifier> &, const NodeIdentifier &) {}

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

} // namespace vnotex
