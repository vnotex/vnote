#include "tagnodelistview.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStandardItemModel>

using namespace vnotex;

TagNodeListView::TagNodeListView(QWidget *p_parent) : QListView(p_parent) {
  m_model = new QStandardItemModel(this);
  setModel(m_model);
  setupView();
}

void TagNodeListView::setupView() {
  setContextMenuPolicy(Qt::CustomContextMenu);
  setEditTriggers(QAbstractItemView::NoEditTriggers);
  setSelectionMode(QAbstractItemView::SingleSelection);

  connect(this, &QListView::doubleClicked, this, &TagNodeListView::onItemActivated);
  connect(this, &QListView::customContextMenuRequested, this,
          &TagNodeListView::onContextMenuRequested);
}

void TagNodeListView::setNodes(const QJsonArray &p_nodes) {
  m_model->clear();

  for (const QJsonValue &val : p_nodes) {
    const QJsonObject obj = val.toObject();
    const QString path = obj.value(QStringLiteral("path")).toString();
    const QString notebookId = obj.value(QStringLiteral("notebookId")).toString();

    auto *item = new QStandardItem(path);
    item->setData(notebookId, NotebookIdRole);
    item->setData(path, RelativePathRole);
    m_model->appendRow(item);
  }
}

NodeIdentifier TagNodeListView::nodeIdFromIndex(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return NodeIdentifier();
  }
  NodeIdentifier nodeId;
  nodeId.notebookId = p_index.data(NotebookIdRole).toString();
  nodeId.relativePath = p_index.data(RelativePathRole).toString();
  return nodeId;
}

void TagNodeListView::onItemActivated(const QModelIndex &p_index) {
  const NodeIdentifier nodeId = nodeIdFromIndex(p_index);
  if (nodeId.isValid()) {
    emit nodeActivated(nodeId);
  }
}

void TagNodeListView::onContextMenuRequested(const QPoint &p_pos) {
  const QModelIndex idx = indexAt(p_pos);
  const NodeIdentifier nodeId = nodeIdFromIndex(idx);
  emit contextMenuRequested(nodeId, viewport()->mapToGlobal(p_pos));
}
