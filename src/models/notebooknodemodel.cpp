#include "notebooknodemodel.h"

#include <QBrush>
#include <QDebug>
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

void NotebookNodeModel::setDisplayRoot(const NodeIdentifier &p_folderId) {
  if (m_displayRoot == p_folderId) {
    return;
  }

  beginResetModel();
  m_displayRoot = p_folderId;
  m_nodeCache.clear();
  m_childrenCache.clear();
  m_fetchedNodes.clear();
  m_indexIdCache.clear();
  m_indexIdLookup.clear();
  m_nextIndexId = 1;
  endResetModel();
}

NodeIdentifier NotebookNodeModel::getDisplayRoot() const {
  return m_displayRoot;
}

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
  // If display root is set, use it as the virtual root
  if (m_displayRoot.isValid()) {
    return m_displayRoot;
  }
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
    // Preview is fetched lazily via PreviewRole when requested by view
    children.append(info);
  }

  return children;
}

QVector<NodeInfo> NotebookNodeModel::parseExternalNodesFromJson(
    const QJsonObject &p_json, const NodeIdentifier &p_parentId) const {
  QVector<NodeInfo> externals;

  // External folders first (sorted alphabetically)
  QJsonArray folders = p_json.value(QStringLiteral("folders")).toArray();
  QVector<QString> folderNames;
  for (const QJsonValue &folderVal : folders) {
    folderNames.append(folderVal.toObject().value(QStringLiteral("name")).toString());
  }
  std::sort(folderNames.begin(), folderNames.end(), [](const QString &a, const QString &b) {
    return a.compare(b, Qt::CaseInsensitive) < 0;
  });

  for (const QString &name : folderNames) {
    NodeInfo info;
    info.id.notebookId = p_parentId.notebookId;
    info.isExternal = true;
    info.name = name;
    if (p_parentId.relativePath.isEmpty()) {
      info.id.relativePath = name;
    } else {
      info.id.relativePath = p_parentId.relativePath + QLatin1Char('/') + name;
    }
    info.isFolder = true;
    // External folders have no child count (opaque until indexed)
    info.childCount = 0;
    externals.append(info);
  }

  // External files second (sorted alphabetically)
  QJsonArray files = p_json.value(QStringLiteral("files")).toArray();
  QVector<QString> fileNames;
  for (const QJsonValue &fileVal : files) {
    fileNames.append(fileVal.toObject().value(QStringLiteral("name")).toString());
  }
  std::sort(fileNames.begin(), fileNames.end(), [](const QString &a, const QString &b) {
    return a.compare(b, Qt::CaseInsensitive) < 0;
  });

  for (const QString &name : fileNames) {
    NodeInfo info;
    info.id.notebookId = p_parentId.notebookId;
    info.isExternal = true;
    info.name = name;
    if (p_parentId.relativePath.isEmpty()) {
      info.id.relativePath = name;
    } else {
      info.id.relativePath = p_parentId.relativePath + QLatin1Char('/') + name;
    }
    info.isFolder = false;
    externals.append(info);
  }

  return externals;
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
    if (info.isExternal) {
      return QStringLiteral("[External] ") + info.relativePath();
    }
    return info.relativePath();

  case Qt::ForegroundRole:
    // External nodes have semi-transparent text to visually differentiate them
    // Note: This is mainly for views that don't use custom delegates
    if (info.isExternal) {
      auto *themeService = m_services.get<ThemeService>();
      if (themeService) {
        QString externalFg = themeService->paletteColor(
            QStringLiteral("widgets#notebookexplorer#external_node_text#fg"));
        if (!externalFg.isEmpty()) {
          return QBrush(QColor(externalFg));
        }
      }
      // Fallback: use a semi-transparent gray if no theme color defined
      return QBrush(QColor(128, 128, 128, 160));
    }
    return QVariant();

  case NodeInfoRole:
    return QVariant::fromValue(info);

  case IsFolderRole:
    return info.isFolder;

  case NodeIdentifierRole:
    return QVariant::fromValue(nodeId);

  case IsExternalRole:
    return info.isExternal;

  case ChildCountRole:
    return info.childCount;

  case PathRole:
    return info.relativePath();

  case ModifiedTimeRole:
    return info.modifiedTimeUtc;

  case CreatedTimeRole:
    return info.createdTimeUtc;

  case PreviewRole:
    // Lazy-load preview for files only
    if (!info.isFolder) {
      // Check if preview already cached
      if (info.preview.isEmpty()) {
        // Fetch from service and cache in NodeInfo
        auto *notebookService = m_services.get<NotebookService>();
        if (notebookService) {
          // Cast away const to update cache (mutable pattern)
          NodeInfo &mutableInfo = const_cast<NodeInfo &>(nodeIt.value());
          mutableInfo.preview = notebookService->peekFile(info.id.notebookId, info.id.relativePath);
          return mutableInfo.preview;
        }
      }
      return info.preview;
    }
    return QString();

  default:
    return QVariant();
  }
}
bool NotebookNodeModel::setData(const QModelIndex &p_index, const QVariant &p_value, int p_role) {
  if (!p_index.isValid() || p_role != Qt::EditRole) {
    return false;
  }

  QString newName = p_value.toString().trimmed();
  if (newName.isEmpty()) {
    return false;
  }

  NodeIdentifier nodeId = nodeIdFromIndex(p_index);
  auto nodeIt = m_nodeCache.find(nodeId);
  if (nodeIt == m_nodeCache.end()) {
    return false;
  }

  const NodeInfo &info = nodeIt.value();

  // Skip if name unchanged
  if (info.name == newName) {
    return false;
  }

  auto *notebookService = m_services.get<NotebookService>();
  if (!notebookService) {
    return false;
  }

  bool success = false;
  if (info.isFolder) {
    success = notebookService->renameFolder(nodeId.notebookId, nodeId.relativePath, newName);
  } else {
    success = notebookService->renameFile(nodeId.notebookId, nodeId.relativePath, newName);
  }

  if (success) {
    // Update cache with new name and path
    NodeInfo &mutableInfo = nodeIt.value();
    mutableInfo.name = newName;

    // Update the relativePath in the node identifier
    QString parentPath = nodeId.parentPath();
    QString newRelativePath = parentPath.isEmpty() ? newName : parentPath + QLatin1Char('/') + newName;
    
    // Remove old cache entries
    NodeIdentifier newNodeId;
    newNodeId.notebookId = nodeId.notebookId;
    newNodeId.relativePath = newRelativePath;

    // Move cache entry to new key
    NodeInfo updatedInfo = mutableInfo;
    updatedInfo.id = newNodeId;
    m_nodeCache.remove(nodeId);
    m_nodeCache.insert(newNodeId, updatedInfo);

    // Update index ID caches
    quintptr indexId = m_indexIdCache.value(nodeId);
    m_indexIdCache.remove(nodeId);
    m_indexIdCache.insert(newNodeId, indexId);
    m_indexIdLookup.insert(indexId, newNodeId);

    // Update parent's children cache
    NodeIdentifier parentId;
    parentId.notebookId = nodeId.notebookId;
    parentId.relativePath = parentPath;
    auto childrenIt = m_childrenCache.find(parentId);
    if (childrenIt != m_childrenCache.end()) {
      for (int i = 0; i < childrenIt->size(); ++i) {
        if (childrenIt->at(i) == nodeId) {
          (*childrenIt)[i] = newNodeId;
          break;
        }
      }
    }

    emit dataChanged(p_index, p_index, {Qt::DisplayRole, Qt::EditRole});
    return true;
  }

  // Emit error signal for UI notification
  emit errorOccurred(tr("Error"), tr("Failed to rename \"%1\".").arg(info.name));
  return false;
}


Qt::ItemFlags NotebookNodeModel::flags(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return Qt::NoItemFlags;
  }

  Qt::ItemFlags defaultFlags = QAbstractItemModel::flags(p_index);

  NodeIdentifier nodeId = nodeIdFromIndex(p_index);
  auto nodeIt = m_nodeCache.find(nodeId);
  if (nodeIt != m_nodeCache.end()) {
    const NodeInfo &nodeInfo = nodeIt.value();
    // External nodes have limited flags (no drag/drop/edit)
    if (nodeInfo.isExternal) {
      // External nodes can only be selected
      return defaultFlags;
    }

    // Enable drag for indexed nodes
    defaultFlags |= Qt::ItemIsDragEnabled;

    // Enable drop only on folders
    if (nodeInfo.isFolder) {
      defaultFlags |= Qt::ItemIsDropEnabled;
    }

    // All indexed nodes are editable (rename)
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

  const NodeInfo &parentInfo = nodeIt.value();
  if (!parentInfo.isFolder) {
    return false;
  }

  // External folders are NOT expandable (opaque until indexed)
  if (parentInfo.isExternal) {
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

  const NodeInfo &parentInfo = nodeIt.value();
  if (!parentInfo.isFolder) {
    return false;
  }

  // External folders cannot be expanded
  if (parentInfo.isExternal) {
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

  // External nodes are only shown at the indexed parent level (not inside external folders)
  // So only fetch external nodes if parent is NOT external
  // Root node is never external; for other nodes, check the cache
  bool parentIsExternal = false;
  if (p_parent.isValid()) {
    auto nodeIt = m_nodeCache.find(parentId);
    if (nodeIt != m_nodeCache.end()) {
      parentIsExternal = nodeIt.value().isExternal;
    }
  }
  bool fetchExternal = m_externalNodesVisible && !parentIsExternal;

  auto *notebookService = m_services.get<NotebookService>();
  if (!notebookService) {
    return;
  }

  // Fetch external nodes first (shown before indexed nodes)
  QVector<NodeInfo> allNodes;
  if (fetchExternal) {
    QJsonObject externalResult =
        notebookService->listFolderExternal(parentId.notebookId, parentId.relativePath);
    QVector<NodeInfo> externalNodes = parseExternalNodesFromJson(externalResult, parentId);
    allNodes.append(externalNodes);
  }

  // Fetch indexed nodes
  QJsonObject result =
      notebookService->listFolderChildren(parentId.notebookId, parentId.relativePath);
  QVector<NodeInfo> children = parseChildrenFromJson(result, parentId);
  allNodes.append(children);

  if (!allNodes.isEmpty()) {
    beginInsertRows(p_parent, 0, allNodes.size() - 1);
  }

  QVector<NodeIdentifier> childIds;
  childIds.reserve(allNodes.size());
  for (const NodeInfo &info : allNodes) {
    m_nodeCache.insert(info.id, info);
    childIds.append(info.id);
  }

  m_childrenCache.insert(parentId, childIds);
  auto parentInfoIt = m_nodeCache.find(parentId);
  if (parentInfoIt != m_nodeCache.end()) {
    parentInfoIt.value().childCount = childIds.size();
  }
  markNodeFetched(parentId);

  if (!allNodes.isEmpty()) {
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

    // Skip external folders - they are not indexed and have no children in the database
    if (childIt.value().isExternal) {
      continue;
    }

    if (isNodeFetched(childId)) {
      continue;
    }

    // Fetch external nodes first if enabled (just like fetchMore does)
    QVector<NodeInfo> allGrandchildren;
    if (m_externalNodesVisible) {
      QJsonObject externalResult =
          notebookService->listFolderExternal(childId.notebookId, childId.relativePath);
      QVector<NodeInfo> externalNodes = parseExternalNodesFromJson(externalResult, childId);
      allGrandchildren.append(externalNodes);
    }

    // Then fetch indexed children
    QJsonObject result =
        notebookService->listFolderChildren(childId.notebookId, childId.relativePath);
    QVector<NodeInfo> indexedChildren = parseChildrenFromJson(result, childId);
    allGrandchildren.append(indexedChildren);

    QVector<NodeIdentifier> grandchildIds;
    grandchildIds.reserve(allGrandchildren.size());
    for (const NodeInfo &info : allGrandchildren) {
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
  QString iconFile = themeService->getIconFile(iconName);

  // External nodes use a different icon color from theme
  if (p_info.isExternal) {
    QString externalFg =
        themeService->paletteColor(QStringLiteral("widgets#notebookexplorer#external_node_icon#fg"));
    if (!externalFg.isEmpty()) {
      return IconUtils::fetchIcon(iconFile, externalFg);
    }
  }

  return IconUtils::fetchIcon(iconFile);
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

  // Ensure the node is in cache (may be loading a folder not yet fetched)
  auto nodeIt = m_nodeCache.find(p_nodeId);
  if (nodeIt == m_nodeCache.end()) {
    // Node not in cache - add it as a folder placeholder so we can load its children
    NodeInfo info;
    info.id = p_nodeId;
    info.isFolder = true;
    info.name = p_nodeId.relativePath.isEmpty() 
        ? QStringLiteral("Root") 
        : p_nodeId.relativePath.section(QLatin1Char('/'), -1);
    m_nodeCache.insert(p_nodeId, info);
    nodeIt = m_nodeCache.find(p_nodeId);
  }

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
        QVector<NodeInfo> allNodes;

        // Fetch external nodes first if visible (and parent is not external)
        bool parentIsExternal = nodeIt.value().isExternal;
        if (m_externalNodesVisible && !parentIsExternal) {
          QJsonObject externalResult =
              notebookService->listFolderExternal(p_nodeId.notebookId, p_nodeId.relativePath);
          QVector<NodeInfo> externalNodes = parseExternalNodesFromJson(externalResult, p_nodeId);
          allNodes.append(externalNodes);
        }

        // Fetch indexed nodes
        QJsonObject result =
            notebookService->listFolderChildren(p_nodeId.notebookId, p_nodeId.relativePath);
        QVector<NodeInfo> childrenInfo = parseChildrenFromJson(result, p_nodeId);
        allNodes.append(childrenInfo);

        QVector<NodeIdentifier> childIds;
        childIds.reserve(allNodes.size());
        for (const NodeInfo &info : allNodes) {
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

void NotebookNodeModel::setExternalNodesVisible(bool p_visible) {
  if (m_externalNodesVisible == p_visible) {
    return;
  }

  m_externalNodesVisible = p_visible;
  // Reload to show/hide external nodes
  reload();
}

bool NotebookNodeModel::isExternalNodesVisible() const { return m_externalNodesVisible; }
