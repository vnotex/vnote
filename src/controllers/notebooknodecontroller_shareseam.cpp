// Pure eligibility predicate for the "Share Folder" context-menu action.
//
// Deliberately isolated in its own translation unit: notebooknodecontroller.cpp
// pulls in ~18 transitive service dependencies, so a test that only wants this
// predicate would otherwise have to link the whole controller stack. Compiling
// just this TU gives controller tests REAL production coverage instead of a
// hand-mirrored copy of the logic.
//
// Same architectural pattern as notebookexplorer2_sortseam.cpp.

#include "notebooknodecontroller.h"

#include <QLatin1String>
#include <QString>

#include <nodeinfo.h>

using namespace vnotex;

bool NotebookNodeController::isFolderShareEligible(const NodeInfo &p_nodeInfo,
                                                   bool p_notebookIsBundled,
                                                   bool p_singleEffectiveSelection) {
  if (!p_nodeInfo.isValid() || !p_nodeInfo.isFolder) {
    return false;
  }

  // External (unindexed) nodes have no metadata subtree to copy, and a missing
  // node has no content to copy.
  if (p_nodeInfo.isExternal || p_nodeInfo.isMissing) {
    return false;
  }

  // The notebook root is not a shareable subfolder: the bundle would have to
  // carry the notebook itself, which this feature explicitly does not do.
  const QString relativePath = p_nodeInfo.id.relativePath;
  if (relativePath.isEmpty() || relativePath == QLatin1String(".")) {
    return false;
  }

  // Only bundled notebooks have the parallel vx_notebook/contents metadata tree
  // the bundle is built from.
  if (!p_notebookIsBundled) {
    return false;
  }

  // Exactly one bundle is produced per invocation.
  //
  // Read-only state is deliberately NOT a gate: sharing is a read operation and
  // is allowed from a read-only bundled notebook.
  return p_singleEffectiveSelection;
}
