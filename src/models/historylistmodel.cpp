#include "historylistmodel.h"

#include <QHash>

#include <core/services/historyservice.h>

using namespace vnotex;

HistoryListModel::HistoryListModel(HistoryService *p_historyService, QObject *p_parent)
    : QAbstractListModel(p_parent), m_historyService(p_historyService) {
}

void HistoryListModel::resetNodesPreservingPreviews(QVector<NodeInfo> p_newNodes) {
  // Carry over previews already lazily loaded (via peekFile in data()) for nodes
  // that survive the reload, so a re-show / date-change does not re-read them.
  if (!m_nodes.isEmpty()) {
    QHash<NodeIdentifier, QString> cachedPreviews;
    cachedPreviews.reserve(m_nodes.size());
    for (const auto &node : m_nodes) {
      if (!node.preview.isEmpty()) {
        cachedPreviews.insert(node.id, node.preview);
      }
    }
    if (!cachedPreviews.isEmpty()) {
      for (auto &node : p_newNodes) {
        if (node.preview.isEmpty()) {
          const auto it = cachedPreviews.constFind(node.id);
          if (it != cachedPreviews.constEnd()) {
            node.preview = it.value();
          }
        }
      }
    }
  }

  beginResetModel();
  m_nodes = std::move(p_newNodes);
  endResetModel();
}

void HistoryListModel::loadHistory() {
  resetNodesPreservingPreviews(m_historyService ? m_historyService->getAllHistory()
                                                : QVector<NodeInfo>());
}

void HistoryListModel::loadRecent(int p_limit) {
  resetNodesPreservingPreviews(m_historyService ? m_historyService->getRecentHistory(p_limit)
                                                : QVector<NodeInfo>());
}

void HistoryListModel::loadForDate(const QDate &p_date, int p_limit) {
  resetNodesPreservingPreviews(
      m_historyService ? m_historyService->getHistoryForDate(p_date, p_limit)
                       : QVector<NodeInfo>());
}

int HistoryListModel::rowCount(const QModelIndex &p_parent) const {
  if (p_parent.isValid()) {
    return 0;
  }
  return m_nodes.size();
}

int HistoryListModel::columnCount(const QModelIndex &p_parent) const {
  if (p_parent.isValid()) {
    return 0;
  }
  return 1;
}

QVariant HistoryListModel::data(const QModelIndex &p_index, int p_role) const {
  if (!p_index.isValid() || p_index.row() < 0 || p_index.row() >= m_nodes.size()) {
    return QVariant();
  }

  const int row = p_index.row();
  const NodeInfo &info = m_nodes[row];

  switch (p_role) {
  case INodeListModel::NodeInfoRole:
    return QVariant::fromValue(info);

  case INodeListModel::IsFolderRole:
    return false;

  case INodeListModel::NodeIdentifierRole:
    return QVariant::fromValue(info.id);

  case INodeListModel::IsExternalRole:
    return false;

  case INodeListModel::PathRole:
    return info.id.relativePath;

  case INodeListModel::PreviewRole:
    if (info.preview.isEmpty() && m_historyService) {
      NodeInfo &mutableInfo = const_cast<NodeInfo &>(m_nodes[row]);
      mutableInfo.preview = m_historyService->previewFor(info.id);
    }
    return info.preview;

  case Qt::DisplayRole:
    return info.name;

  case Qt::ToolTipRole:
    return info.id.relativePath;

  default:
    return QVariant();
  }
}

NodeIdentifier HistoryListModel::nodeIdFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid() || p_index.row() < 0 || p_index.row() >= m_nodes.size()) {
    return NodeIdentifier();
  }
  return m_nodes[p_index.row()].id;
}

NodeInfo HistoryListModel::nodeInfoFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid() || p_index.row() < 0 || p_index.row() >= m_nodes.size()) {
    return NodeInfo();
  }
  return m_nodes[p_index.row()];
}

QModelIndex HistoryListModel::indexFromNodeId(const NodeIdentifier &p_nodeId) const {
  for (int i = 0; i < m_nodes.size(); ++i) {
    if (m_nodes[i].id == p_nodeId) {
      return index(i, 0);
    }
  }
  return QModelIndex();
}

QString HistoryListModel::getNotebookId() const {
  return QString();
}
