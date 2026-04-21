#include "historylistmodel.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>

#include <core/services/notebookcoreservice.h>

using namespace vnotex;

HistoryListModel::HistoryListModel(NotebookCoreService *p_notebookService, QObject *p_parent)
    : QAbstractListModel(p_parent), m_notebookService(p_notebookService) {
}

void HistoryListModel::loadHistory() {
  beginResetModel();
  m_nodes.clear();

  if (!m_notebookService) {
    endResetModel();
    return;
  }

  const QJsonArray notebooks = m_notebookService->listNotebooks();
  for (const auto &nbVal : notebooks) {
    const QJsonObject nbObj = nbVal.toObject();
    const QString notebookId = nbObj.value(QStringLiteral("id")).toString();
    if (notebookId.isEmpty()) {
      continue;
    }

    const QJsonArray history = m_notebookService->getHistoryResolved(notebookId);
    for (const auto &entryVal : history) {
      const QJsonObject entry = entryVal.toObject();

      NodeInfo info;
      info.id.notebookId = notebookId;
      info.id.relativePath = entry.value(QStringLiteral("relativePath")).toString();
      info.name = entry.value(QStringLiteral("name")).toString();
      info.isFolder = false;
      info.isExternal = false;
      info.modifiedTimeUtc =
          QDateTime::fromMSecsSinceEpoch(
              static_cast<qint64>(entry.value(QStringLiteral("openedUtc")).toDouble()));

      m_nodes.append(info);
    }
  }

  // Sort by openedUtc descending (most recent first).
  std::sort(m_nodes.begin(), m_nodes.end(),
            [](const NodeInfo &a, const NodeInfo &b) {
              return a.modifiedTimeUtc > b.modifiedTimeUtc;
            });

  endResetModel();
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
    if (info.preview.isEmpty() && m_notebookService) {
      NodeInfo &mutableInfo = const_cast<NodeInfo &>(m_nodes[row]);
      mutableInfo.preview = m_notebookService->peekFile(info.id.notebookId, info.id.relativePath);
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
