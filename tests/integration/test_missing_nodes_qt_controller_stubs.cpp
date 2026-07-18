// test_missing_nodes_qt_controller_stubs.cpp
//
// Stub bodies for the few external symbols that notebooknodecontroller.cpp
// (compiled into test_missing_nodes_qt for the T11 controller subtests)
// references but which the test does NOT exercise. The controller's real
// dependency graph (services, pathutils, logging, configs) is satisfied by
// core_services + core_configs; these three families live in TUs the test
// deliberately does NOT compile:
//   - NotebookNodeView (view layer)        -> selectedNodeIds/selectNode/expandToNode
//   - MessageBoxHelper (widgets)           -> questionSaveDiscardCancel
//   - ProcessUtils  (utils, not in a lib)  -> startDetached
//
// None of the controller code paths the T11 subtests invoke (openNodes
// preflight, handleMissingRemovalConfirmed, suppressMissingNodes, the
// missingNodesDetected re-emit) reach these symbols at runtime; they only need
// to LINK. No widget is ever constructed, so the test stays GUILESS.
//
// Compiled ONLY into the test target — production uses the real definitions.

#include <QString>

#include <utils/processutils.h>
#include <views/notebooknodeview.h>
#include <widgets/messageboxhelper.h>

namespace vnotex {

void ProcessUtils::startDetached(const QString &) {}
void ProcessUtils::startDetached(const QString &, const QStringList &) {}
QStringList ProcessUtils::parseCombinedArgString(const QString &) { return {}; }

int MessageBoxHelper::questionSaveDiscardCancel(MessageBoxHelper::Type, const QString &,
                                                const QString &, const QString &, QWidget *) {
  return 0;
}

QList<NodeIdentifier> NotebookNodeView::selectedNodeIds() const { return {}; }
void NotebookNodeView::selectNode(const NodeIdentifier &) {}
void NotebookNodeView::expandToNode(const NodeIdentifier &) {}

} // namespace vnotex
