#include "tagcontroller.h"

#include <QJsonObject>
#include <QJsonValue>

#include <QSet>
#include <QVector>

#include <core/servicelocator.h>
#include <core/services/tagcoreservice.h>
#include <core/services/tagservice.h>

using namespace vnotex;

TagController::TagController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
}

void TagController::setNotebookId(const QString &p_notebookId) {
  m_notebookId = p_notebookId;
}

void TagController::onTagsSelected(const QStringList &p_selectedTags) {
  if (p_selectedTags.isEmpty()) {
    m_matchingNodes = QJsonArray();
    emit matchingNodesChanged(QJsonArray());
    emit tagCompatibilityChanged(QStringList());
    return;
  }

  const QString notebookId = m_notebookId;
  if (notebookId.isEmpty()) {
    emit errorOccurred(tr("Tags"), tr("No notebook selected."));
    m_matchingNodes = QJsonArray();
    emit matchingNodesChanged(QJsonArray());
    emit tagCompatibilityChanged(QStringList());
    return;
  }

  auto *tagService = m_services.get<TagService>();
  auto *tagCoreService = m_services.get<TagCoreService>();
  if (!tagService || !tagCoreService) {
    emit errorOccurred(tr("Tags"), tr("Tag service is not available."));
    m_matchingNodes = QJsonArray();
    emit matchingNodesChanged(QJsonArray());
    emit tagCompatibilityChanged(QStringList());
    return;
  }

  QJsonArray rawNodes = tagService->findFilesByTags(notebookId, p_selectedTags);

  // Inject notebookId into each match object — vxcore doesn't include it.
  m_matchingNodes = QJsonArray();
  for (const QJsonValue &val : rawNodes) {
    QJsonObject obj = val.toObject();
    if (!obj.contains(QLatin1String("notebookId"))) {
      obj.insert(QLatin1String("notebookId"), notebookId);
    }
    m_matchingNodes.append(obj);
  }
  emit matchingNodesChanged(m_matchingNodes);

  QSet<QString> selectedTagSet;
  for (const auto &tag : p_selectedTags) {
    selectedTagSet.insert(tag);
  }
  QStringList incompatibleTags;

  QVector<QJsonArray> pendingArrays;
  pendingArrays.append(tagCoreService->listTags(notebookId));

  while (!pendingArrays.isEmpty()) {
    QJsonArray tags = pendingArrays.last();
    pendingArrays.removeLast();
    for (const auto &tagValue : tags) {
      const QString tagName = extractTagName(tagValue);
      if (!tagName.isEmpty() && !selectedTagSet.contains(tagName)) {
        QStringList queryTags = p_selectedTags;
        queryTags.append(tagName);
        if (tagCoreService->findFilesByTags(notebookId, queryTags).isEmpty()) {
          incompatibleTags.append(tagName);
        }
      }

      const QJsonObject tagObj = tagValue.toObject();
      const QJsonArray children = tagObj.value(QString(QLatin1String("children"))).toArray();
      if (!children.isEmpty()) {
        pendingArrays.append(children);
      }
    }
  }

  incompatibleTags.removeDuplicates();
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

  for (const auto &nodeValue : m_matchingNodes) {
    NodeIdentifier nodeId = extractNodeIdentifier(nodeValue);
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

void TagController::handleDeleteTagConfirmed(const QString &p_notebookId, const QString &p_tagName) {
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

QString TagController::extractTagName(const QJsonValue &p_tagValue) const {
  if (p_tagValue.isString()) {
    return p_tagValue.toString();
  }

  const QJsonObject tagObj = p_tagValue.toObject();
  return tagObj.value(QString(QLatin1String("name"))).toString();
}

NodeIdentifier TagController::extractNodeIdentifier(const QJsonValue &p_nodeValue) const {
  NodeIdentifier nodeId;

  const QJsonObject nodeObj = p_nodeValue.toObject();
  if (nodeObj.isEmpty()) {
    return nodeId;
  }

  const QString notebookId = nodeObj.value(QString(QLatin1String("notebookId"))).toString();
  if (!notebookId.isEmpty()) {
    nodeId.notebookId = notebookId;
  } else {
    nodeId.notebookId = m_notebookId;
  }

  const QString relativePath = nodeObj.value(QString(QLatin1String("relativePath"))).toString();
  if (!relativePath.isEmpty()) {
    nodeId.relativePath = relativePath;
    return nodeId;
  }

  const QString filePath = nodeObj.value(QString(QLatin1String("filePath"))).toString();
  if (!filePath.isEmpty()) {
    nodeId.relativePath = filePath;
    return nodeId;
  }

  const QString path = nodeObj.value(QString(QLatin1String("path"))).toString();
  if (!path.isEmpty()) {
    nodeId.relativePath = path;
  }

  return nodeId;
}
