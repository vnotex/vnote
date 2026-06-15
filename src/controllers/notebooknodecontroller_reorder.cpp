// notebooknodecontroller_reorder.cpp
//
// Holds the NotebookNodeController constructor, destructor, the drag-reorder
// dispatch path (T7), and the sortNodes signal emitter (T8) — all part of the
// notebook-explorer-drag-reorder plan.
//
// Why this lives in its own TU: the rest of NotebookNodeController pulls in
// the view / model / theme / process-utils dependency graph (~20 transitive
// .cpp files). The reorder unit test
// (tests/controllers/test_notebook_node_controller_reorder.cpp) only needs the
// reorder + sort code paths. Compiling THIS file standalone — together with
// core_services + Qt6::Widgets — is enough to construct, exercise, and tear
// down a controller without recompiling the entire UI stack. The vtable +
// QObject machinery land here because the destructor is the first out-of-line
// virtual method in this TU.

#include "notebooknodecontroller.h"

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <models/inodelistmodel.h>

using namespace vnotex;

NotebookNodeController::NotebookNodeController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_clipboard(new ClipboardState()) {
  // T7: connect ONCE to reorderCompleted so every reorderNodes() call routes
  // its async result through the same slot. Wiring lives here (not inside
  // reorderNodes) to avoid accumulating duplicate connections per call.
  if (auto *notebookService = m_services.get<NotebookCoreService>()) {
    connect(notebookService, &NotebookCoreService::reorderCompleted, this,
            &NotebookNodeController::onReorderCompleted);
  }
}

NotebookNodeController::~NotebookNodeController() {}

// T8: setModel/model live here so the reorder test target (which compiles
// only this TU) can configure a stub model for the sortNodes/currentNotebookId
// path. Production callers (NotebookExplorer2, etc.) link against these
// definitions unchanged.
void NotebookNodeController::setModel(INodeListModel *p_model) { m_model = p_model; }

INodeListModel *NotebookNodeController::model() const { return m_model; }

// T8: lives here (moved from notebooknodecontroller.cpp) so sortNodes() can
// reach it without forcing the reorder unit test target to compile the full
// controller TU. All production callers (newNote, newFolder,
// addCopyMoveActions, sortNodes) link against this definition unchanged.
QString NotebookNodeController::currentNotebookId() const {
  if (m_model) {
    return m_model->getNotebookId();
  }
  return QString();
}

// T8: sortNodes is now a thin signal emitter. Per src/controllers/AGENTS.md
// (and the locked plan decision) controllers MUST NOT show QDialog. We emit
// sortRequested; NotebookExplorer2::onSortRequested (T11) owns SortDialog2
// and, on user confirmation, calls reorderNodes() with the chosen order.
// Mirrors the newNote -> newNoteRequested pattern.
void NotebookNodeController::sortNodes(const NodeIdentifier &p_parentId) {
  NodeIdentifier parentId = p_parentId;
  if (!parentId.isValid()) {
    parentId.notebookId = currentNotebookId();
    parentId.relativePath = QString(); // notebook root
  }
  if (parentId.notebookId.isEmpty()) {
    return; // No active notebook -> no-op
  }
  emit sortRequested(parentId);
}

void NotebookNodeController::reorderNodes(const NodeIdentifier &p_parentId,
                                          const QList<NodeIdentifier> &p_orderedFolderIds,
                                          const QList<NodeIdentifier> &p_orderedFileIds) {
  // Validation 1: empty input → hard no-op. The service has its own no-op
  // check but the controller MUST short-circuit BEFORE dispatch so callers
  // get deterministic "nothing happened" behavior (no signal, no service
  // touched). Matches the contract documented in notebooknodecontroller.h.
  if (p_orderedFolderIds.isEmpty() && p_orderedFileIds.isEmpty()) {
    return;
  }

  // Validation 2 & 3: every id must live directly under p_parentId. Mixed
  // parents (or stray descendants from deeper folders) are a UX-level error,
  // not a vxcore-level one — vxcore would reject only on permutation mismatch
  // and would not produce a useful message about cross-parent drops. Reject
  // here so we can surface a translated message.
  auto extractParent = [](const QString &p_relPath) -> QString {
    const int lastSep = p_relPath.lastIndexOf(QLatin1Char('/'));
    return lastSep < 0 ? QString() : p_relPath.left(lastSep);
  };

  auto parentMatches = [&](const NodeIdentifier &p_id) -> bool {
    return p_id.notebookId == p_parentId.notebookId &&
           extractParent(p_id.relativePath) == p_parentId.relativePath;
  };

  for (const auto &id : p_orderedFolderIds) {
    if (!parentMatches(id)) {
      emit errorOccurred(tr("Reorder"), tr("Mixed parents not allowed"));
      return;
    }
  }
  for (const auto &id : p_orderedFileIds) {
    if (!parentMatches(id)) {
      emit errorOccurred(tr("Reorder"), tr("Mixed parents not allowed"));
      return;
    }
  }

  // Convert NodeIdentifier lists → name QStringLists (last path component).
  // vxcore_folder_set_children_order is keyed by file/folder NAME within the
  // parent folder, not by full relative path.
  auto toName = [](const QString &p_relPath) -> QString {
    const int lastSep = p_relPath.lastIndexOf(QLatin1Char('/'));
    return lastSep < 0 ? p_relPath : p_relPath.mid(lastSep + 1);
  };

  QStringList orderedFolderNames;
  orderedFolderNames.reserve(p_orderedFolderIds.size());
  for (const auto &id : p_orderedFolderIds) {
    orderedFolderNames.append(toName(id.relativePath));
  }

  QStringList orderedFileNames;
  orderedFileNames.reserve(p_orderedFileIds.size());
  for (const auto &id : p_orderedFileIds) {
    orderedFileNames.append(toName(id.relativePath));
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    emit errorOccurred(tr("Reorder"), tr("Notebook service not available."));
    return;
  }

  // Async dispatch — result flows back via onReorderCompleted (constructor
  // wired the connection). Returns immediately.
  notebookService->reorderFolderChildren(p_parentId.notebookId, p_parentId.relativePath,
                                         orderedFolderNames, orderedFileNames);
}

void NotebookNodeController::onReorderCompleted(const QString &p_notebookId,
                                                const QString &p_folderRelPath, bool p_success,
                                                const QString &p_errorMessage) {
  NodeIdentifier parentId;
  parentId.notebookId = p_notebookId;
  parentId.relativePath = p_folderRelPath;
  if (p_success) {
    emit nodesReordered(parentId);
  } else {
    // Reuse the existing errorOccurred signal so the View can surface the
    // failure through its standard error-message channel. The service's
    // translated message is forwarded verbatim.
    emit errorOccurred(tr("Reorder Failed"), p_errorMessage);
  }
}
