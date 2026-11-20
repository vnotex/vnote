#include "tagcontroller.h"

#include <QSet>

#include <core/servicelocator.h>
#include <core/services/tagcoreservice.h>
#include <core/services/tagservice.h>

using namespace vnotex;

TagController::TagController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  qRegisterMetaType<QVector<TagFileMatch>>();
}

void TagController::setNotebookId(const QString &p_notebookId) { m_notebookId = p_notebookId; }

void TagController::onTagsSelected(const QStringList &p_selectedTags) {
  if (p_selectedTags.isEmpty()) {
    m_matchingNodes.clear();
    emit matchingNodesChanged(m_matchingNodes);
    emit tagCompatibilityChanged(QStringList());
    return;
  }

  const QString notebookId = m_notebookId;
  if (notebookId.isEmpty()) {
    emit errorOccurred(tr("Tags"), tr("No notebook selected."));
    m_matchingNodes.clear();
    emit matchingNodesChanged(m_matchingNodes);
    emit tagCompatibilityChanged(QStringList());
    return;
  }

  auto *tagService = m_services.get<TagService>();
  auto *tagCoreService = m_services.get<TagCoreService>();
  if (!tagService || !tagCoreService) {
    emit errorOccurred(tr("Tags"), tr("Tag service is not available."));
    m_matchingNodes.clear();
    emit matchingNodesChanged(m_matchingNodes);
    emit tagCompatibilityChanged(QStringList());
    return;
  }

  m_matchingNodes = tagService->findFilesByTags(notebookId, p_selectedTags);

  // Native matches have no notebook attribution; preserve explicit per-item values.
  QSet<QString> compatibleTags;
  for (auto &node : m_matchingNodes) {
    if (node.m_notebookId.isNull()) {
      node.m_notebookId = notebookId;
    }
    // Each match contains its complete tag set. A tag can extend this AND selection
    // exactly when it occurs on one of the notes already matching the selection.
    for (const auto &tag : node.m_tags) {
      compatibleTags.insert(tag);
    }
  }
  emit matchingNodesChanged(m_matchingNodes);

  QSet<QString> selectedTagSet;
  for (const auto &tag : p_selectedTags) {
    selectedTagSet.insert(tag);
  }
  QStringList incompatibleTags;

  const auto allTags = tagCoreService->listTags(notebookId);
  for (const auto &tag : allTags) {
    const auto &tagName = tag.m_name;
    if (!tagName.isEmpty() && !selectedTagSet.contains(tagName) &&
        !compatibleTags.contains(tagName)) {
      incompatibleTags.append(tagName);
    }
  }

  emit tagCompatibilityChanged(incompatibleTags);
}

void TagController::onNodeActivated(const NodeIdentifier &p_nodeId) {
  if (p_nodeId.isValid()) {
    NodeIdentifier nodeId;
    nodeId.notebookId = p_nodeId.notebookId;
    nodeId.relativePath = p_nodeId.relativePath;
    emit openNodeRequested(nodeId);
    return;
  }

  for (const auto &node : m_matchingNodes) {
    NodeIdentifier nodeId;
    nodeId.notebookId = node.m_notebookId.isNull() ? m_notebookId : node.m_notebookId;
    nodeId.relativePath = node.m_filePath;
    if (nodeId.isValid()) {
      emit openNodeRequested(nodeId);
      return;
    }
  }
}

void TagController::onNewTagAction(const QString &p_notebookId) {
  const QString notebookId = resolveNotebookId(p_notebookId);
  if (notebookId.isEmpty()) {
    emit errorOccurred(tr("Tags"), tr("No notebook selected."));
    return;
  }

  emit newTagRequested(notebookId);
}

void TagController::onDeleteTagAction(const QString &p_notebookId, const QString &p_tagName) {
  const QString notebookId = resolveNotebookId(p_notebookId);
  if (notebookId.isEmpty()) {
    emit errorOccurred(tr("Tags"), tr("No notebook selected."));
    return;
  }

  if (p_tagName.isEmpty()) {
    emit errorOccurred(tr("Delete Tag"), tr("Tag name cannot be empty."));
    return;
  }

  emit deleteTagRequested(notebookId, p_tagName);
}

void TagController::onMoveTagAction(const QString &p_notebookId, const QString &p_tagName,
                                    const QString &p_newParent) {
  const QString notebookId = resolveNotebookId(p_notebookId);
  if (notebookId.isEmpty()) {
    emit errorOccurred(tr("Tags"), tr("No notebook selected."));
    return;
  }

  if (p_tagName.isEmpty()) {
    emit errorOccurred(tr("Move Tag"), tr("Tag name cannot be empty."));
    return;
  }

  auto *tagService = m_services.get<TagService>();
  if (!tagService) {
    emit errorOccurred(tr("Move Tag"), tr("Tag service is not available."));
    return;
  }

  if (!tagService->moveTag(notebookId, p_tagName, p_newParent)) {
    emit errorOccurred(tr("Move Tag"), tr("Failed to move tag: ") + p_tagName);
    return;
  }

  emit tagOperationCompleted();
  emit tagMoved(notebookId, p_tagName);
}

void TagController::handleNewTagResult(const QString &p_notebookId, const QString &p_tagName) {
  const QString notebookId = resolveNotebookId(p_notebookId);
  if (notebookId.isEmpty()) {
    emit errorOccurred(tr("New Tag"), tr("No notebook selected."));
    return;
  }

  if (p_tagName.isEmpty()) {
    emit errorOccurred(tr("New Tag"), tr("Tag name cannot be empty."));
    return;
  }

  auto *tagService = m_services.get<TagService>();
  if (!tagService) {
    emit errorOccurred(tr("New Tag"), tr("Tag service is not available."));
    return;
  }

  if (!tagService->createTagPath(notebookId, p_tagName)) {
    emit errorOccurred(tr("New Tag"), tr("Failed to create tag: ") + p_tagName);
    return;
  }

  emit tagOperationCompleted();

  // Extract the leaf tag name (last component of the path) so the view can locate it.
  const QString leafTag = p_tagName.section(QLatin1Char('/'), -1);
  if (!leafTag.isEmpty()) {
    emit tagCreated(leafTag);
  }
}

void TagController::handleDeleteTagConfirmed(const QString &p_notebookId,
                                             const QString &p_tagName) {
  const QString notebookId = resolveNotebookId(p_notebookId);
  if (notebookId.isEmpty()) {
    emit errorOccurred(tr("Delete Tag"), tr("No notebook selected."));
    return;
  }

  if (p_tagName.isEmpty()) {
    emit errorOccurred(tr("Delete Tag"), tr("Tag name cannot be empty."));
    return;
  }

  auto *tagService = m_services.get<TagService>();
  if (!tagService) {
    emit errorOccurred(tr("Delete Tag"), tr("Tag service is not available."));
    return;
  }

  if (!tagService->deleteTag(notebookId, p_tagName)) {
    emit errorOccurred(tr("Delete Tag"), tr("Failed to delete tag: ") + p_tagName);
    return;
  }

  emit tagOperationCompleted();
}

void TagController::handleMoveTagResult(const QString &p_notebookId, const QString &p_tagName,
                                        const QString &p_newParent) {
  onMoveTagAction(p_notebookId, p_tagName, p_newParent);
}

QString TagController::resolveNotebookId(const QString &p_notebookId) const {
  if (!p_notebookId.isEmpty()) {
    return p_notebookId;
  }
  return m_notebookId;
}
