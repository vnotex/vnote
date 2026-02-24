#include "notebooknodemodel.h"

#include <QMimeData>

#include <core/notebook/node.h>
#include <core/notebook/notebook.h>
#include <core/vnotex.h>
#include <utils/iconutils.h>
#include <widgets/widgetsfactory.h>
#include <core/nodeinfo.h>

using namespace vnotex;

namespace {
const QString c_nodeMimeType = QStringLiteral("application/x-vnotex-node");
} // namespace

NotebookNodeModel::NotebookNodeModel(QObject *p_parent) : QAbstractItemModel(p_parent) {}

NotebookNodeModel::~NotebookNodeModel() {}

void NotebookNodeModel::setNotebook(const QSharedPointer<Notebook> &p_notebook) {
  beginResetModel();

  m_notebook = p_notebook;
  m_fetchedNodes.clear();

  endResetModel();

  emit notebookChanged();
}

QSharedPointer<Notebook> NotebookNodeModel::getNotebook() const { return m_notebook; }

QModelIndex NotebookNodeModel::index(int p_row, int p_column, const QModelIndex &p_parent) const {
  if (!m_notebook || p_row < 0 || p_column < 0) {
    return QModelIndex();
  }

  Node *parentNode = nodeFromIndex(p_parent);
  if (!parentNode) {
    // Root level - get from notebook root
    auto rootNode = m_notebook->getRootNode().data();
    if (!rootNode) {
      return QModelIndex();
    }
    const auto &children = rootNode->getChildrenRef();
    if (p_row >= children.size()) {
      return QModelIndex();
    }
    return createIndex(p_row, p_column, children[p_row].data());
  }

  // Non-root level
  const auto &children = parentNode->getChildrenRef();
  if (p_row >= children.size()) {
    return QModelIndex();
  }
  return createIndex(p_row, p_column, children[p_row].data());
}

QModelIndex NotebookNodeModel::parent(const QModelIndex &p_child) const {
  if (!p_child.isValid()) {
    return QModelIndex();
  }

  Node *childNode = nodeFromIndex(p_child);
  if (!childNode) {
    return QModelIndex();
  }

  Node *parentNode = childNode->getParent();
  if (!parentNode || parentNode->isRoot()) {
    // Parent is root, which is invisible
    return QModelIndex();
  }

  // Find the row of parent within grandparent
  Node *grandParent = parentNode->getParent();
  if (!grandParent) {
    return QModelIndex();
  }

  const auto &siblings = grandParent->getChildrenRef();
  for (int i = 0; i < siblings.size(); ++i) {
    if (siblings[i].data() == parentNode) {
      return createIndex(i, 0, parentNode);
    }
  }

  return QModelIndex();
}

int NotebookNodeModel::rowCount(const QModelIndex &p_parent) const {
  if (!m_notebook) {
    return 0;
  }

  if (p_parent.column() > 0) {
    return 0;
  }

  Node *parentNode = nodeFromIndex(p_parent);
  if (!parentNode) {
    // Root level
    auto rootNode = m_notebook->getRootNode().data();
    if (!rootNode) {
      return 0;
    }
    // Only return count if fetched, for lazy loading
    if (!isNodeFetched(rootNode)) {
      return 0;
    }
    return rootNode->getChildrenCount();
  }

  if (!parentNode->isContainer()) {
    return 0;
  }

  // Only return count if fetched
  if (!isNodeFetched(parentNode)) {
    return 0;
  }

  return parentNode->getChildrenCount();
}

int NotebookNodeModel::columnCount(const QModelIndex &p_parent) const {
  Q_UNUSED(p_parent);
  return 1; // Single column for tree view
}

QVariant NotebookNodeModel::data(const QModelIndex &p_index, int p_role) const {
  if (!p_index.isValid()) {
    return QVariant();
  }

  Node *node = nodeFromIndex(p_index);
  if (!node) {
    return QVariant();
  }

  switch (p_role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    return node->getName();

  case Qt::DecorationRole:
    return getNodeIcon(node);

  case Qt::ToolTipRole:
    return node->fetchAbsolutePath();

  case NodeInfoRole:
    return QVariant::fromValue(nodeToNodeInfo(node));

  case IsFolderRole:
    return node->isContainer();

  case NodeRole:
    return QVariant::fromValue(static_cast<void *>(node));

  case NodeTypeRole:
    return static_cast<int>(node->getFlags());

  case IsContainerRole:
    return node->isContainer();

  case ChildCountRole:
    return node->getChildrenCount();

  case PathRole:
    return node->fetchAbsolutePath();

  case ModifiedTimeRole:
    return node->getModifiedTimeUtc();

  case CreatedTimeRole:
    return node->getCreatedTimeUtc();

  default:
    return QVariant();
  }
}
Qt::ItemFlags NotebookNodeModel::flags(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return Qt::NoItemFlags;
  }

  Qt::ItemFlags defaultFlags = QAbstractItemModel::flags(p_index);

  Node *node = nodeFromIndex(p_index);
  if (node) {
    // Enable drag
    defaultFlags |= Qt::ItemIsDragEnabled;

    // Enable drop only on containers
    if (node->isContainer()) {
      defaultFlags |= Qt::ItemIsDropEnabled;
    }

    // Read-only nodes can't be edited
    if (!node->isReadOnly()) {
      defaultFlags |= Qt::ItemIsEditable;
    }
  }

  return defaultFlags;
}

bool NotebookNodeModel::hasChildren(const QModelIndex &p_parent) const {
  if (!m_notebook) {
    return false;
  }

  Node *parentNode = nodeFromIndex(p_parent);
  if (!parentNode) {
    // Root level - check notebook root
    auto rootNode = m_notebook->getRootNode().data();
    return rootNode && rootNode->getChildrenCount() > 0;
  }

  if (!parentNode->isContainer()) {
    return false;
  }

  return parentNode->getChildrenCount() > 0;
}

bool NotebookNodeModel::canFetchMore(const QModelIndex &p_parent) const {
  if (!m_notebook) {
    return false;
  }

  Node *parentNode = nodeFromIndex(p_parent);
  if (!parentNode) {
    // Root level
    auto rootNode = m_notebook->getRootNode().data();
    return rootNode && !isNodeFetched(rootNode) && rootNode->getChildrenCount() > 0;
  }

  if (!parentNode->isContainer()) {
    return false;
  }

  return !isNodeFetched(parentNode) && parentNode->getChildrenCount() > 0;
}

void NotebookNodeModel::fetchMore(const QModelIndex &p_parent) {
  if (!m_notebook) {
    return;
  }

  Node *parentNode = nodeFromIndex(p_parent);
  Node *targetNode = nullptr;

  if (!parentNode) {
    // Root level
    targetNode = m_notebook->getRootNode().data();
  } else {
    targetNode = parentNode;
  }

  if (!targetNode || !targetNode->isContainer()) {
    return;
  }

  if (isNodeFetched(targetNode)) {
    return;
  }

  // Load node if not loaded
  if (!targetNode->isLoaded()) {
    targetNode->load();
  }

  int childCount = targetNode->getChildrenCount();
  if (childCount > 0) {
    beginInsertRows(p_parent, 0, childCount - 1);
    markNodeFetched(targetNode);
    endInsertRows();
  } else {
    markNodeFetched(targetNode);
  }
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
      Node *node = nodeFromIndex(index);
      if (node) {
        // Store node pointer as quintptr
        stream << reinterpret_cast<quintptr>(node);
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

  Node *targetNode = nodeFromIndex(p_parent);
  if (!targetNode) {
    // Dropping on root
    targetNode = m_notebook->getRootNode().data();
  }

  if (!targetNode || !targetNode->isContainer()) {
    return false;
  }

  // Decode the dragged nodes
  QByteArray encodedData = p_data->data(c_nodeMimeType);
  QDataStream stream(&encodedData, QIODevice::ReadOnly);

  QList<Node *> draggedNodes;
  while (!stream.atEnd()) {
    quintptr nodePtr;
    stream >> nodePtr;
    Node *node = reinterpret_cast<Node *>(nodePtr);
    if (node) {
      draggedNodes.append(node);
    }
  }

  // The actual move/copy operation should be handled by the controller
  // This model just validates the drop target
  // Return false to indicate the model doesn't handle the actual operation
  // The view/controller should handle it via signals

  return false;
}

Node *NotebookNodeModel::nodeFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return nullptr;
  }
  return static_cast<Node *>(p_index.internalPointer());
}

QModelIndex NotebookNodeModel::indexFromNode(Node *p_node) const {
  if (!p_node || !m_notebook) {
    return QModelIndex();
  }

  if (p_node->isRoot()) {
    return QModelIndex();
  }

  Node *parent = p_node->getParent();
  if (!parent) {
    return QModelIndex();
  }

  const auto &siblings = parent->getChildrenRef();
  for (int i = 0; i < siblings.size(); ++i) {
    if (siblings[i].data() == p_node) {
      return createIndex(i, 0, p_node);
    }
  }

  return QModelIndex();
}

void NotebookNodeModel::reload() {
  beginResetModel();
  m_fetchedNodes.clear();
  if (m_notebook) {
    m_notebook->reloadNodes();
  }
  endResetModel();
}

void NotebookNodeModel::reloadNode(Node *p_node) {
  if (!p_node) {
    reload();
    return;
  }

  QModelIndex nodeIndex = indexFromNode(p_node);

  if (p_node->isContainer()) {
    // Remove existing children from model
    int childCount = p_node->getChildrenCount();
    if (childCount > 0 && isNodeFetched(p_node)) {
      beginRemoveRows(nodeIndex, 0, childCount - 1);
      m_fetchedNodes.remove(p_node);
      endRemoveRows();
    }

    // Reload from backend
    p_node->load();

    // Re-add children
    int newChildCount = p_node->getChildrenCount();
    if (newChildCount > 0) {
      beginInsertRows(nodeIndex, 0, newChildCount - 1);
      markNodeFetched(p_node);
      endInsertRows();
    }
  }

  // Notify data change for the node itself
  emit dataChanged(nodeIndex, nodeIndex);
}

void NotebookNodeModel::nodeDataChanged(Node *p_node) {
  if (!p_node) {
    return;
  }

  QModelIndex nodeIndex = indexFromNode(p_node);
  if (nodeIndex.isValid()) {
    emit dataChanged(nodeIndex, nodeIndex);
  }
}

bool NotebookNodeModel::isNodeFetched(Node *p_node) const {
  return m_fetchedNodes.contains(p_node);
}

void NotebookNodeModel::markNodeFetched(Node *p_node) { m_fetchedNodes.insert(p_node); }

QVector<QSharedPointer<Node>> NotebookNodeModel::getNodeChildren(Node *p_node) const {
  if (!p_node) {
    return {};
  }
  return p_node->getChildren();
}

QIcon NotebookNodeModel::getNodeIcon(Node *p_node) const {
  if (!p_node) {
    return QIcon();
  }

  // Use the icon utility from the existing codebase
  const auto &themeMgr = VNoteX::getInst().getThemeMgr();
  QString iconName;

  if (p_node->isContainer()) {
    iconName = QStringLiteral("folder_node.svg");
  } else if (p_node->hasContent()) {
    iconName = QStringLiteral("file_node.svg");
  } else {
    iconName = QStringLiteral("file_node.svg");
  }

  return IconUtils::fetchIcon(themeMgr.getIconFile(iconName));
}

NodeInfo NotebookNodeModel::nodeToNodeInfo(Node *p_node) const {
  NodeInfo info;
  if (!p_node) {
    return info;
  }

  auto *notebook = p_node->getNotebook();
  if (notebook) {
    info.notebookId = QString::number(notebook->getId());
  }

  if (p_node->isRoot()) {
    info.relativePath = QString();
  } else {
    // Get path relative to notebook root
    QString absPath = p_node->fetchAbsolutePath();
    QString rootPath = notebook ? notebook->getRootFolderAbsolutePath() : QString();
    if (!rootPath.isEmpty() && absPath.startsWith(rootPath)) {
      info.relativePath = absPath.mid(rootPath.length());
      if (info.relativePath.startsWith(QLatin1Char('/'))) {
        info.relativePath = info.relativePath.mid(1);
      }
    } else {
      info.relativePath = p_node->getName();
    }
  }

  info.isFolder = p_node->isContainer();
  info.name = p_node->getName();
  info.createdTimeUtc = p_node->getCreatedTimeUtc();
  info.modifiedTimeUtc = p_node->getModifiedTimeUtc();
  info.childCount = p_node->isContainer() ? p_node->getChildrenCount() : 0;
  info.tags = p_node->getTags();

  return info;
}
