#include "tagfilemodel.h"

#include <QJsonObject>

using namespace vnotex;

TagFileModel::TagFileModel(QObject *p_parent)
    : QAbstractListModel(p_parent) {
}

void TagFileModel::setNodes(const QJsonArray &p_nodes, const QString &p_notebookId) {
  beginResetModel();

  m_nodes.clear();
  m_notebookId = p_notebookId;

  for (const auto &nodeVal : p_nodes) {
    const QJsonObject obj = nodeVal.toObject();
    const QString filePath = obj.value(QStringLiteral("filePath")).toString();

    NodeInfo info;
    info.id.notebookId =
        obj.contains(QStringLiteral("notebookId"))
            ? obj.value(QStringLiteral("notebookId")).toString()
            : p_notebookId;
    info.id.relativePath = filePath;
    info.isFolder = false;
    info.isExternal = false;

    // fileName from JSON, fallback to last component of filePath.
    QString fileName = obj.value(QStringLiteral("fileName")).toString();
    if (fileName.isEmpty() && !filePath.isEmpty()) {
      int lastSlash = filePath.lastIndexOf(QLatin1Char('/'));
      fileName = (lastSlash >= 0) ? filePath.mid(lastSlash + 1) : filePath;
    }
    info.name = fileName;

    // Parse tags JSON array to QStringList.
    const QJsonArray tagsArray = obj.value(QStringLiteral("tags")).toArray();
    for (const auto &tagVal : tagsArray) {
      const QString tag = tagVal.toString();
      if (!tag.isEmpty()) {
        info.tags.append(tag);
      }
    }

    m_nodes.append(info);
  }

  endResetModel();
}

int TagFileModel::rowCount(const QModelIndex &p_parent) const {
  if (p_parent.isValid()) {
    return 0;
  }
  return m_nodes.size();
}

int TagFileModel::columnCount(const QModelIndex &p_parent) const {
  if (p_parent.isValid()) {
    return 0;
  }
  return 1;
}

QVariant TagFileModel::data(const QModelIndex &p_index, int p_role) const {
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
    return QString();

  case Qt::DisplayRole:
    return info.name;

  case Qt::ToolTipRole:
    return info.id.relativePath;

  default:
    return QVariant();
  }
}

NodeIdentifier TagFileModel::nodeIdFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid() || p_index.row() < 0 || p_index.row() >= m_nodes.size()) {
    return NodeIdentifier();
  }
  return m_nodes[p_index.row()].id;
}

NodeInfo TagFileModel::nodeInfoFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid() || p_index.row() < 0 || p_index.row() >= m_nodes.size()) {
    return NodeInfo();
  }
  return m_nodes[p_index.row()];
}

QModelIndex TagFileModel::indexFromNodeId(const NodeIdentifier &p_nodeId) const {
  for (int i = 0; i < m_nodes.size(); ++i) {
    if (m_nodes[i].id == p_nodeId) {
      return index(i, 0);
    }
  }
  return QModelIndex();
}

QString TagFileModel::getNotebookId() const {
  return m_notebookId;
}
