#ifndef NOTEBOOKEXPLORER2_SORTSEAM_H
#define NOTEBOOKEXPLORER2_SORTSEAM_H

#include <QJsonObject>
#include <QStringList>

#include <core/nodeidentifier.h>

class QWidget;

namespace vnotex {

// T11 (notebook-explorer-drag-reorder): testable seam for
// NotebookExplorer2::onSortRequested. Lives in its own header / TU so widget
// tests can link the dialog-orchestration logic without dragging in the rest
// of NotebookExplorer2 (which transitively pulls in ConfigMgr2, SyncService,
// ThemeService, BufferService, ~20 dialog deps, and the MainWindow shell).
//
// Free function in the `vnotex` namespace — NOT a member of NotebookExplorer2
// — so the test's AUTOMOC pass does not generate qt_metacall references to
// every slot in NotebookExplorer2 (which would then need the full source TU
// linked in to resolve).
//
// Behavior:
//   1. Parses p_childrenJson (shape: {"folders":[{name,...}], "files":[...]}).
//   2. Shows a SortDialog2 for folders (only if non-empty); if the user
//      accepts with a CHANGED order, records the new order.
//   3. Shows a SortDialog2 for files (only if non-empty) with the same
//      change-detection.
//   4. Returns the (possibly empty) ordered name lists. The caller is
//      responsible for skipping requestReorderNodes when both are empty.
//
// Modality is Qt::WindowModal (matches NotebookExplorer2 conventions).
// Strict separation: folders and files NEVER share a dialog instance.
struct SortDialogResult {
  QStringList newFolderOrder; // empty = no change (or cancelled)
  QStringList newFileOrder;   // empty = no change (or cancelled)
};

SortDialogResult runSortDialogsForChildren(const NodeIdentifier &p_parentId,
                                           const QJsonObject &p_childrenJson, QWidget *p_parent);

} // namespace vnotex

#endif // NOTEBOOKEXPLORER2_SORTSEAM_H
