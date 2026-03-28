#ifndef TAGFILEMODEL_H
#define TAGFILEMODEL_H

#include <QAbstractListModel>
#include <QJsonArray>
#include <QVector>

#include <core/nodeinfo.h>
#include <models/inodelistmodel.h>

namespace vnotex {

class TagFileModel : public QAbstractListModel, public INodeListModel {
  Q_OBJECT

public:
  explicit TagFileModel(QObject *p_parent = nullptr);
  ~TagFileModel() override = default;

  // Set nodes from tag search JSON results.
  // p_nodes: array of objects with "filePath", "fileName", "tags", optionally "notebookId"
  // p_notebookId: fallback notebook ID if not present in individual objects
  void setNodes(const QJsonArray &p_nodes, const QString &p_notebookId);

  // QAbstractListModel overrides.
  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &p_parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override;

  // INodeListModel overrides.
  NodeIdentifier nodeIdFromIndex(const QModelIndex &p_index) const override;
  NodeInfo nodeInfoFromIndex(const QModelIndex &p_index) const override;
  QModelIndex indexFromNodeId(const NodeIdentifier &p_nodeId) const override;

  bool supportsDragDrop() const override { return false; }
  bool supportsPreview() const override { return false; }
  bool supportsHierarchy() const override { return false; }
  bool supportsExternalNodes() const override { return false; }

  QString getNotebookId() const override;

private:
  QVector<NodeInfo> m_nodes;
  QString m_notebookId;
};

} // namespace vnotex

#endif // TAGFILEMODEL_H
