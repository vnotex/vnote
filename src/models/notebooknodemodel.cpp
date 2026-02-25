#include "notebooknodemodel.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMimeData>
#include <QIODevice>

#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <core/services/notebookservice.h>
#include <gui/services/themeservice.h>
#include <utils/iconutils.h>

using namespace vnotex;

namespace {
const QString c_nodeMimeType = QStringLiteral("application/x-vnotex-node-identifier");
} // namespace

NotebookNodeModel::NotebookNodeModel(ServiceLocator &p_services, QObject *p_parent)
    : QAbstractItemModel(p_parent), m_services(p_services) {}

NotebookNodeModel::~NotebookNodeModel() {}

void NotebookNodeModel::setNotebookId(const QString &p_notebookId) {
  beginResetModel();

  m_notebookId = p_notebookId;
  m_nodeCache.clear();
  m_childrenCache.clear();
  m_fetchedNodes.clear();
  m_indexIdCache.clear();
  m_indexIdLookup.clear();
  m_nextIndexId = 1;

  endResetModel();

  emit notebookChanged();
}

QString NotebookNodeModel::getNotebookId() const { return m_notebookId; }

void NotebookNodeModel::ensureRoot() const {
  if (m_notebookId.isEmpty()) {
    return;
  }

  NodeIdentifier rootId = rootNodeId();
  if (!m_nodeCache.contains(rootId)) {
    NodeInfo info;
    info.id = rootId;
    info.isFolder = true;
    info.name = QStringLiteral("Root");
    m_nodeCache.insert(rootId, info);
  }
}

NodeIdentifier NotebookNodeModel::rootNodeId() const {
  NodeIdentifier rootId;
  rootId.notebookId = m_notebookId;
  rootId.relativePath = QString();
  return rootId;
}

bool NotebookNodeModel::isNodeFetched(const NodeIdentifier &p_nodeId) const {
  return m_fetchedNodes.contains(p_nodeId);
}

void NotebookNodeModel::markNodeFetched(const NodeIdentifier &p_nodeId) {
  if (p_nodeId.isValid()) {
    m_fetchedNodes.insert(p_nodeId);
  }
}

void NotebookNodeModel::removeNodeFromCaches(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  m_nodeCache.remove(p_nodeId);
  m_childrenCache.remove(p_nodeId);
  m_fetchedNodes.remove(p_nodeId);

  auto indexIt = m_indexIdCache.find(p_nodeId);
  if (indexIt != m_indexIdCache.end()) {
    m_indexIdLookup.remove(indexIt.value());
    m_indexIdCache.erase(indexIt);
  }
}

int NotebookNodeModel::childRow(const NodeIdentifier &p_parentId,
                                const NodeIdentifier &p_childId) const {
  auto it = m_childrenCache.find(p_parentId);
  if (it == m_childrenCache.end()) {
    return -1;
  }

  const auto &children = it.value();
  for (int i = 0; i < children.size(); ++i) {
    if (children[i] == p_childId) {
      return i;
    }
  }

  return -1;
}

quintptr NotebookNodeModel::indexIdForNode(const NodeIdentifier &p_nodeId) const {
  if (!p_nodeId.isValid()) {
    return 0;
  }

  auto it = m_indexIdCache.find(p_nodeId);
  if (it != m_indexIdCache.end()) {
    return it.value();
  }

  quintptr newId = m_nextIndexId++;
  m_indexIdCache.insert(p_nodeId, newId);
  m_indexIdLookup.insert(newId, p_nodeId);
  return newId;
}

NodeIdentifier NotebookNodeModel::nodeIdForIndexId(quintptr p_indexId) const {
  auto it = m_indexIdLookup.find(p_indexId);
  if (it == m_indexIdLookup.end()) {
    return NodeIdentifier();
  }
  return it.value();
}

QVector<NodeInfo> NotebookNodeModel::parseChildrenFromJson(
    const QJsonObject &p_json, const NodeIdentifier &p_parentId) const {
  QVector<NodeInfo> children;

  QJsonArray folders = p_json.value(QStringLiteral("folders")).toArray();
  for (const QJsonValue &folderVal : folders) {
    NodeInfo info = parseNodeInfoFromJson(folderVal.toObject(), p_parentId);
    info.isFolder = true;
    children.append(info);
  }

  QJsonArray files = p_json.value(QStringLiteral("files")).toArray();
  for (const QJsonValue &fileVal : files) {
    NodeInfo info = parseNodeInfoFromJson(fileVal.toObject(), p_parentId);
    info.isFolder = false;
    children.append(info);
  }

  return children;
}

NodeInfo NotebookNodeModel::parseNodeInfoFromJson(const QJsonObject &p_json,
                                                  const NodeIdentifier &p_parentId) const {
  NodeInfo info;
  info.id.notebookId = p_parentId.notebookId;

  const QString name = p_json.value(QStringLiteral("name")).toString();
  info.name = name;
  if (p_parentId.relativePath.isEmpty()) {
    info.id.relativePath = name;
  } else {
    info.id.relativePath = p_parentId.relativePath + QLatin1Char('/') + name;
  }

  const QJsonValue createdUtc = p_json.value(QStringLiteral("createdUtc"));
  if (createdUtc.isDouble()) {
    auto createdMs = static_cast<qint64>(createdUtc.toDouble());
    info.createdTimeUtc = QDateTime::fromMSecsSinceEpoch(createdMs, Qt::UTC);
  }

  const QJsonValue modifiedUtc = p_json.value(QStringLiteral("modifiedUtc"));
  if (modifiedUtc.isDouble()) {
    auto modifiedMs = static_cast<qint64>(modifiedUtc.toDouble());
    info.modifiedTimeUtc = QDateTime::fromMSecsSinceEpoch(modifiedMs, Qt::UTC);
  }

  QJsonArray tagsArray = p_json.value(QStringLiteral("tags")).toArray();
  for (const QJsonValue &tagVal : tagsArray) {
    info.tags.append(tagVal.toString());
  }

  QJsonObject metadata = p_json.value(QStringLiteral("metadata")).toObject();
  if (!metadata.isEmpty()) {
    info.backgroundColor = metadata.value(QStringLiteral("backgroundColor")).toString();
    info.borderColor = metadata.value(QStringLiteral("borderColor")).toString();
    info.textColor = metadata.value(QStringLiteral("textColor")).toString();
  }

  return info;
}

QModelIndex NotebookNodeModel::index(int p_row, int p_column, const QModelIndex &p_parent) const {
  if (m_notebookId.isEmpty() || p_row < 0 || p_column < 0) {
    return QModelIndex();
  }

  NodeIdentifier parentId = p_parent.isValid() ? nodeIdFromIndex(p_parent) : rootNodeId();
  auto childrenIt = m_childrenCache.find(parentId);
  if (childrenIt == m_childrenCache.end() || p_row >= childrenIt->size()) {
    return QModelIndex();
  }

  const NodeIdentifier &childId = childrenIt->at(p_row);
  return createIndex(p_row, p_column, indexIdForNode(childId));
}

QModelIndex NotebookNodeModel::parent(const QModelIndex &p_child) const {
  if (!p_child.isValid()) {
    return QModelIndex();
  }

  NodeIdentifier childId = nodeIdFromIndex(p_child);
  if (!childId.isValid() || childId.isRoot()) {
    return QModelIndex();
  }

  // Get parent path
  QString parentPath = childId.parentPath();
  NodeIdentifier parentId;
  parentId.notebookId = m_notebookId;
  parentId.relativePath = parentPath;
  if (parentId.isRoot()) {
    // Parent is root, which is invisible
    return QModelIndex();
  }

  // Find grandparent to determine row of parent
  QString grandParentPath = parentId.parentPath();
  NodeIdentifier grandParentId;
  grandParentId.notebookId = m_notebookId;
  grandParentId.relativePath = grandParentPath;

  int row = childRow(grandParentId, parentId);
  if (row < 0) {
    return QModelIndex();
  }

  return createIndex(row, 0, indexIdForNode(parentId));
}

int NotebookNodeModel::rowCount(const QModelIndex &p_parent) const {
  if (m_notebookId.isEmpty()) {
    return 0;
  }

  if (p_parent.column() > 0) {
    return 0;
  }

  ensureRoot();
  NodeIdentifier parentId = p_parent.isValid() ? nodeIdFromIndex(p_parent) : rootNodeId();
  if (!isNodeFetched(parentId)) {
    return 0;
  }

  return m_childrenCache.value(parentId).size();
}

int NotebookNodeModel::columnCount(const QModelIndex &p_parent) const {
  Q_UNUSED(p_parent);
  return 1; // Single column for tree view
}

QVariant NotebookNodeModel::data(const QModelIndex &p_index, int p_role) const {
  if (!p_index.isValid()) {
    return QVariant();
  }

  NodeIdentifier nodeId = nodeIdFromIndex(p_index);
  auto nodeIt = m_nodeCache.find(nodeId);
  if (nodeIt == m_nodeCache.end()) {
    return QVariant();
  }

  const NodeInfo &info = nodeIt.value();

  switch (p_role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    return info.name;

  case Qt::DecorationRole:
    return getNodeIcon(info);

  case Qt::ToolTipRole:
    return info.relativePath();

  case NodeInfoRole:
    return QVariant::fromValue(info);

  case IsFolderRole:
    return info.isFolder;

  case NodeIdentifierRole:
    return QVariant::fromValue(nodeId);

  case ChildCountRole:
    return info.childCount;

  case PathRole:
    return info.relativePath();

  case ModifiedTimeRole:
    return info.modifiedTimeUtc;

  case CreatedTimeRole:
    return info.createdTimeUtc;

  default:
    return QVariant();
  }
}

Qt::ItemFlags NotebookNodeModel::flags(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return Qt::NoItemFlags;
  }

  Qt::ItemFlags defaultFlags = QAbstractItemModel::flags(p_index);

  NodeIdentifier nodeId = nodeIdFromIndex(p_index);
  auto nodeIt = m_nodeCache.find(nodeId);
  if (nodeIt != m_nodeCache.end()) {
    // Enable drag for all nodes
    defaultFlags |= Qt::ItemIsDragEnabled;

    // Enable drop only on folders
    if (nodeIt.value().isFolder) {
      defaultFlags |= Qt::ItemIsDropEnabled;
    }

    // All nodes are editable (rename)
    defaultFlags |= Qt::ItemIsEditable;
  }

  return defaultFlags;
}

bool NotebookNodeModel::hasChildren(const QModelIndex &p_parent) const {
  if (m_notebookId.isEmpty()) {
    return false;
  }

  ensureRoot();
  NodeIdentifier parentId = p_parent.isValid() ? nodeIdFromIndex(p_parent) : rootNodeId();
  auto nodeIt = m_nodeCache.find(parentId);
  if (nodeIt == m_nodeCache.end() || !nodeIt.value().isFolder) {
    return !p_parent.isValid();
  }

  if (!nodeIt.value().isFolder) {
    return false;
  }

  if (isNodeFetched(parentId)) {
    return !m_childrenCache.value(parentId).isEmpty();
  }

  return true;
}

bool NotebookNodeModel::canFetchMore(const QModelIndex &p_parent) const {
  if (m_notebookId.isEmpty()) {
    return false;
  }

  ensureRoot();
  NodeIdentifier parentId = p_parent.isValid() ? nodeIdFromIndex(p_parent) : rootNodeId();
  auto nodeIt = m_nodeCache.find(parentId);
  if (nodeIt == m_nodeCache.end()) {
    return true;
  }

  if (!nodeIt.value().isFolder) {
    return false;
  }

  return !isNodeFetched(parentId);
}

void NotebookNodeModel::fetchMore(const QModelIndex &p_parent) {
  if (m_notebookId.isEmpty()) {
    return;
  }

  ensureRoot();
  NodeIdentifier parentId = p_parent.isValid() ? nodeIdFromIndex(p_parent) : rootNodeId();
  if (!parentId.isValid() || isNodeFetched(parentId)) {
    return;
  }

  auto *notebookService = m_services.get<NotebookService>();
  if (!notebookService) {
    return;
  }

  QJsonObject result =
      notebookService->listFolderChildren(parentId.notebookId, parentId.relativePath);
  QVector<NodeInfo> children = parseChildrenFromJson(result, parentId);

  if (!children.isEmpty()) {
    beginInsertRows(p_parent, 0, children.size() - 1);
  }

  QVector<NodeIdentifier> childIds;
  childIds.reserve(children.size());
  for (const NodeInfo &info : children) {
    m_nodeCache.insert(info.id, info);
    childIds.append(info.id);
  }

  m_childrenCache.insert(parentId, childIds);
  auto parentInfoIt = m_nodeCache.find(parentId);
  if (parentInfoIt != m_nodeCache.end()) {
    parentInfoIt.value().childCount = childIds.size();
  }
  markNodeFetched(parentId);

  if (!children.isEmpty()) {
    endInsertRows();
  }

  // Prefetch grandchildren so child folders show correct child count
  prefetchChildrenOfChildren(p_parent);
}

void NotebookNodeModel::prefetchChildrenOfChildren(const QModelIndex &p_parent) {
  if (m_notebookId.isEmpty()) {
    return;
  }

  NodeIdentifier parentId = p_parent.isValid() ? nodeIdFromIndex(p_parent) : rootNodeId();
  if (!parentId.isValid() || !isNodeFetched(parentId)) {
    return;
  }

  auto *notebookService = m_services.get<NotebookService>();
  if (!notebookService) {
    return;
  }

  const QVector<NodeIdentifier> &children = m_childrenCache.value(parentId);
  for (const NodeIdentifier &childId : children) {
    auto childIt = m_nodeCache.find(childId);
    if (childIt == m_nodeCache.end() || !childIt.value().isFolder) {
      continue;
    }

    if (isNodeFetched(childId)) {
      continue;
    }

    QJsonObject result =
        notebookService->listFolderChildren(childId.notebookId, childId.relativePath);
    QVector<NodeInfo> grandchildren = parseChildrenFromJson(result, childId);

    QVector<NodeIdentifier> grandchildIds;
    grandchildIds.reserve(grandchildren.size());
    for (const NodeInfo &info : grandchildren) {
      m_nodeCache.insert(info.id, info);
      grandchildIds.append(info.id);
    }

    m_childrenCache.insert(childId, grandchildIds);
    childIt.value().childCount = grandchildIds.size();
    markNodeFetched(childId);

    QModelIndex childIndex = indexFromNodeId(childId);
    if (childIndex.isValid()) {
      emit dataChanged(childIndex, childIndex);
    }
  }
}
QIcon NotebookNodeModel::getNodeIcon(const NodeInfo &p_info) const {
  auto *themeService = m_services.get<ThemeService>();
  if (!themeService) {
    return QIcon();
  }

  QString iconName = p_info.isFolder ? QStringLiteral("folder_node.svg")
                                     : QStringLiteral("file_node.svg");
  return IconUtils::fetchIcon(themeService->getIconFile(iconName));
}

Qt::DropActions NotebookNodeModel::supportedDropActions() const {
  return Qt::MoveAction | Qt::CopyAction;
}

QStringList NotebookNodeModel::mimeTypes() const {
  QStringList types;
  types << c_nodeMimeType;
  types << QStringLiteral("text/uri-list");
  return types;
}

QMimeData *NotebookNodeModel::mimeData(const QModelIndexList &p_indexes) const {
  QMimeData *mimeData = new QMimeData();
  QByteArray encodedData;
  QDataStream stream(&encodedData, QIODevice::WriteOnly);

  for (const QModelIndex &index : p_indexes) {
    if (index.isValid()) {
      NodeIdentifier nodeId = nodeIdFromIndex(index);
      if (nodeId.isValid()) {
        // Serialize NodeIdentifier: notebookId + relativePath
        stream << nodeId.notebookId << nodeId.relativePath;
      }
    }
  }

  mimeData->setData(c_nodeMimeType, encodedData);
  return mimeData;
}

bool NotebookNodeModel::dropMimeData(const QMimeData *p_data, Qt::DropAction p_action, int p_row,
                                     int p_column, const QModelIndex &p_parent) {
  Q_UNUSED(p_row);
  Q_UNUSED(p_column);

  if (p_action == Qt::IgnoreAction) {
    return true;
  }

  if (!p_data->hasFormat(c_nodeMimeType)) {
    // TODO: Handle external file drops (text/uri-list)
    return false;
  }

  NodeIdentifier targetId = p_parent.isValid() ? nodeIdFromIndex(p_parent) : rootNodeId();

  auto nodeIt = m_nodeCache.find(targetId);
  if (nodeIt == m_nodeCache.end() || !nodeIt.value().isFolder) {
    return false;
  }

  // Decode the dragged node identifiers
  QByteArray encodedData = p_data->data(c_nodeMimeType);
  QDataStream stream(&encodedData, QIODevice::ReadOnly);

  QList<NodeIdentifier> draggedNodes;
  while (!stream.atEnd()) {
    QString notebookId, relativePath;
    stream >> notebookId >> relativePath;
    NodeIdentifier nodeId;
    nodeId.notebookId = notebookId;
    nodeId.relativePath = relativePath;
    if (nodeId.isValid()) {
      draggedNodes.append(nodeId);
    }
  }

  // The actual move/copy operation should be handled by the controller
  // This model just validates the drop target
  // Return false to indicate the model doesn't handle the actual operation
  // The view/controller should handle it via signals

  return false;
}

NodeIdentifier NotebookNodeModel::nodeIdFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return NodeIdentifier();
  }

  return nodeIdForIndexId(p_index.internalId());
}

QModelIndex NotebookNodeModel::indexFromNodeId(const NodeIdentifier &p_nodeId) const {
  if (!p_nodeId.isValid() || m_notebookId.isEmpty()) {
    return QModelIndex();
  }

  if (p_nodeId.isRoot()) {
    return QModelIndex();
  }

  // Get parent to find the row
  QString parentPath = p_nodeId.parentPath();
  NodeIdentifier parentId;
  parentId.notebookId = m_notebookId;
  parentId.relativePath = parentPath;

  int row = childRow(parentId, p_nodeId);
  if (row < 0) {
    return QModelIndex();
  }

  return createIndex(row, 0, indexIdForNode(p_nodeId));
}

NodeInfo NotebookNodeModel::nodeInfoFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return NodeInfo();
  }

  NodeIdentifier nodeId = nodeIdFromIndex(p_index);
  auto nodeIt = m_nodeCache.find(nodeId);
  if (nodeIt == m_nodeCache.end()) {
    return NodeInfo();
  }

  return nodeIt.value();
}

void NotebookNodeModel::reload() {
  beginResetModel();
  m_nodeCache.clear();
  m_childrenCache.clear();
  m_fetchedNodes.clear();
  m_indexIdCache.clear();
  m_indexIdLookup.clear();
  m_nextIndexId = 1;
  endResetModel();
}

void NotebookNodeModel::reloadNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    reload();
    return;
  }

  QModelIndex nodeIndex = indexFromNodeId(p_nodeId);

  auto nodeIt = m_nodeCache.find(p_nodeId);
  if (nodeIt != m_nodeCache.end() && nodeIt.value().isFolder) {
    // Remove existing children from model
    const auto children = m_childrenCache.value(p_nodeId);
    int childCount = children.size();
    if (isNodeFetched(p_nodeId)) {
      if (childCount > 0) {
        beginRemoveRows(nodeIndex, 0, childCount - 1);
        // Remove children from cache
        for (const NodeIdentifier &childId : children) {
          removeNodeFromCaches(childId);
        }
        endRemoveRows();
      }
      // Always clear children cache and fetched state when reloading
      m_childrenCache.remove(p_nodeId);
      m_fetchedNodes.remove(p_nodeId);
    }

    if (!m_notebookId.isEmpty()) {
      auto *notebookService = m_services.get<NotebookService>();
      if (notebookService) {
        QJsonObject result =
            notebookService->listFolderChildren(p_nodeId.notebookId, p_nodeId.relativePath);
        QVector<NodeInfo> childrenInfo = parseChildrenFromJson(result, p_nodeId);
        QVector<NodeIdentifier> childIds;
        childIds.reserve(childrenInfo.size());
        for (const NodeInfo &info : childrenInfo) {
          m_nodeCache.insert(info.id, info);
          childIds.append(info.id);
        }
        m_childrenCache.insert(p_nodeId, childIds);
        nodeIt.value().childCount = childIds.size();
      }
    }

    // Re-add children
    int newChildCount = m_childrenCache.value(p_nodeId).size();
    if (newChildCount > 0) {
      beginInsertRows(nodeIndex, 0, newChildCount - 1);
      markNodeFetched(p_nodeId);
      endInsertRows();
    } else {
      markNodeFetched(p_nodeId);
    }
  }

  // Notify data change for the node itself
  emit dataChanged(nodeIndex, nodeIndex);

  // Prefetch grandchildren so child folders show correct child count
  prefetchChildrenOfChildren(nodeIndex);
}

void NotebookNodeModel::nodeDataChanged(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  QModelIndex nodeIndex = indexFromNodeId(p_nodeId);
  if (nodeIndex.isValid()) {
    emit dataChanged(nodeIndex, nodeIndex);
  }
}
