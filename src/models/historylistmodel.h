#ifndef HISTORYLISTMODEL_H
#define HISTORYLISTMODEL_H

#include <QAbstractListModel>
#include <QVector>

#include <core/nodeinfo.h>
#include <models/inodelistmodel.h>

namespace vnotex {

class NotebookCoreService;

class HistoryListModel : public QAbstractListModel, public INodeListModel {
  Q_OBJECT

public:
  explicit HistoryListModel(NotebookCoreService *p_notebookService,
                            QObject *p_parent = nullptr);
  ~HistoryListModel() override = default;

  // Fetch and merge history from all open notebooks.
  void loadHistory();

  // QAbstractListModel overrides.
  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &p_parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override;

  // INodeListModel overrides.
  NodeIdentifier nodeIdFromIndex(const QModelIndex &p_index) const override;
  NodeInfo nodeInfoFromIndex(const QModelIndex &p_index) const override;
  QModelIndex indexFromNodeId(const NodeIdentifier &p_nodeId) const override;

  bool supportsDragDrop() const override { return false; }
  bool supportsPreview() const override { return true; }
  bool supportsHierarchy() const override { return false; }
  bool supportsExternalNodes() const override { return false; }

  QString getNotebookId() const override;

private:
  NotebookCoreService *m_notebookService = nullptr;
  QVector<NodeInfo> m_nodes;
};

} // namespace vnotex

#endif // HISTORYLISTMODEL_H
