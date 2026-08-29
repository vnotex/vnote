// Pure helpers behind the batch "Tags" context-menu action: the target filter
// (filterTagTargets) and the per-file delta planner (planTagDelta).
//
// Deliberately isolated in their own translation unit for the same reason as
// notebooknodecontroller_shareseam.cpp: notebooknodecontroller.cpp pulls in ~18
// transitive service dependencies, so a GUILESS controller test that only wants
// these decisions would otherwise have to link the whole controller stack.
// Compiling just this TU gives the tests REAL production coverage instead of a
// hand-mirrored copy of the logic.

#include "notebooknodecontroller.h"

#include <algorithm>

#include <core/nodeidentifier.h>
#include <nodeinfo.h>

using namespace vnotex;

QList<NodeIdentifier>
NotebookNodeController::filterTagTargets(const QList<NodeIdentifier> &p_resolvedSelection,
                                         const NodeIdentifier &p_clickedId,
                                         const NodeInfoLookup &p_infoLookup) {
  QList<NodeIdentifier> targets;
  if (!p_clickedId.isValid() || !p_infoLookup) {
    return targets;
  }

  for (const auto &id : p_resolvedSelection) {
    // A batch spans exactly one notebook: the dialog lists that notebook's tags.
    if (id.notebookId != p_clickedId.notebookId) {
      continue;
    }

    // NodeIdentifier::isValid() only checks a non-empty notebookId, and an
    // unresolved node yields a DEFAULT NodeInfo whose isFolder is false — so
    // NodeInfo::isValid() is the discriminator that rejects a stale id.
    const NodeInfo info = p_infoLookup(id);
    if (!info.isValid() || info.isFolder || info.isRoot()) {
      continue;
    }

    if (!targets.contains(id)) {
      targets.append(id);
    }
  }

  return targets;
}

NotebookNodeController::TagDeltaOps
NotebookNodeController::planTagDelta(const QSet<QString> &p_currentTags,
                                     const QSet<QString> &p_added, const QSet<QString> &p_removed) {
  TagDeltaOps ops;

  for (const auto &tag : p_added) {
    // Already on this file: issuing TagFile would return
    // VXCORE_ERR_ALREADY_EXISTS, which is a routine no-op for a delta derived
    // from a Partial tag, not a failure. Skip it so every failure that does come
    // back from the service is genuine.
    if (!p_currentTags.contains(tag)) {
      ops.toAdd.append(tag);
    }
  }

  for (const auto &tag : p_removed) {
    // Not on this file: UntagFile would return VXCORE_ERR_NOT_FOUND. Same
    // reasoning as above.
    if (p_currentTags.contains(tag)) {
      ops.toRemove.append(tag);
    }
  }

  // QSet iteration order is unspecified; sort so the issued order (and any test
  // assertion over it) is deterministic.
  std::sort(ops.toAdd.begin(), ops.toAdd.end());
  std::sort(ops.toRemove.begin(), ops.toRemove.end());
  return ops;
}
bool NotebookNodeController::applyTagDelta(const TagDeltaIo &p_io, const QSet<QString> &p_added,
                                           const QSet<QString> &p_removed) {
  if (p_added.isEmpty() && p_removed.isEmpty()) {
    return true;
  }
  if (!p_io.readTags || !p_io.tagFile || !p_io.untagFile) {
    return false;
  }

  // Read this file's CURRENT tags so the ops that would be no-ops on it can be
  // dropped. This is a pre-check that SKIPS work; it is NOT a read-modify-write
  // of the tag array -- nothing is written back wholesale, so a tag added
  // concurrently (or by a hook) survives. Because the redundant ops never reach
  // the service, every failure that does come back is a GENUINE failure and is
  // reported as one. Confirming state AFTER a failure would instead mask a
  // persistence error, since vxcore mutates its cached FileRecord before saving.
  QSet<QString> currentTags;
  if (!p_io.readTags(currentTags)) {
    // The current tags are unknown, and "unknown" is indistinguishable from
    // "none" -- fail loud rather than guess, and issue nothing.
    return false;
  }

  const TagDeltaOps ops = planTagDelta(currentTags, p_added, p_removed);

  bool allOk = true;
  for (const auto &tag : ops.toAdd) {
    if (!p_io.tagFile(tag)) {
      allOk = false;
    }
  }
  for (const auto &tag : ops.toRemove) {
    if (!p_io.untagFile(tag)) {
      allOk = false;
    }
  }
  return allOk;
}