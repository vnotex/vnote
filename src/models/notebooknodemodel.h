#ifndef NOTEBOOKNODEMODEL_H
#define NOTEBOOKNODEMODEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QIcon>
#include <QMap>
#include <QSet>
#include <QVector>

#include <core/nodeinfo.h>
#include <core/servicelocator.h>

namespace vnotex {

inline bool operator<(const NodeIdentifier &p_left, const NodeIdentifier &p_right) {
  if (p_left.notebookId != p_right.notebookId) {
    return p_left.notebookId < p_right.notebookId;
  }
  return p_left.relativePath < p_right.relativePath;
}

// A QAbstractItemModel implementation that exposes notebook node hierarchy
// to Qt's Model/View framework. Uses NodeIdentifier/NodeInfo instead of Node*.
// Data is fetched from NotebookService via ServiceLocator.
class NotebookNodeModel : public QAbstractItemModel {
  Q_OBJECT

public:
  // Custom data roles for node information
  enum Roles {
    // New architecture roles (preferred)
    NodeInfoRole = Qt::UserRole + 1, // NodeInfo struct
    IsFolderRole,                    // bool - is folder
    NodeIdentifierRole,              // NodeIdentifier struct
    IsExternalRole,                  // bool - is external (unindexed) node

    // Display roles
    ChildCountRole,   // int - number of children
    PathRole,         // QString - relative node path
    ModifiedTimeRole, // QDateTime
    CreatedTimeRole,  // QDateTime
    PreviewRole       // QString - file content preview (lazy-loaded, files only)
  };

  explicit NotebookNodeModel(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~NotebookNodeModel() override;

  // Core QAbstractItemModel interface
  QModelIndex index(int p_row, int p_column,
                    const QModelIndex &p_parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex &p_child) const override;
  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &p_parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex &p_index) const override;
  bool setData(const QModelIndex &p_index, const QVariant &p_value,
               int p_role = Qt::EditRole) override;

  // Lazy loading support
  bool hasChildren(const QModelIndex &p_parent = QModelIndex()) const override;
  bool canFetchMore(const QModelIndex &p_parent) const override;
  void fetchMore(const QModelIndex &p_parent) override;

  // Drag & drop support
  Qt::DropActions supportedDropActions() const override;
  QStringList mimeTypes() const override;
  QMimeData *mimeData(const QModelIndexList &p_indexes) const override;
  bool dropMimeData(const QMimeData *p_data, Qt::DropAction p_action, int p_row, int p_column,
                    const QModelIndex &p_parent) override;

  // Custom API
  void setNotebookId(const QString &p_notebookId);
  QString getNotebookId() const;

  // Set display root for flat view modes (e.g., file list showing one folder's contents)
  // When set, this folder's children appear at the model's root level
  void setDisplayRoot(const NodeIdentifier &p_folderId);
  NodeIdentifier getDisplayRoot() const;

  // NodeIdentifier <-> Index conversion
  NodeIdentifier nodeIdFromIndex(const QModelIndex &p_index) const;
  NodeInfo nodeInfoFromIndex(const QModelIndex &p_index) const;
  QModelIndex indexFromNodeId(const NodeIdentifier &p_nodeId) const;

  // Reload operations
  void reload();
  void reloadNode(const NodeIdentifier &p_nodeId);
  void prefetchChildrenOfChildren(const QModelIndex &p_parent);

  // Notify model about external changes to a node
  void nodeDataChanged(const NodeIdentifier &p_nodeId);

  // External nodes visibility
  void setExternalNodesVisible(bool p_visible);
  bool isExternalNodesVisible() const;

signals:
  void notebookChanged();
  void errorOccurred(const QString &p_title, const QString &p_message);

private:
  void ensureRoot() const;
  NodeIdentifier rootNodeId() const;
  bool isNodeFetched(const NodeIdentifier &p_nodeId) const;
  void markNodeFetched(const NodeIdentifier &p_nodeId);
  void removeNodeFromCaches(const NodeIdentifier &p_nodeId);
  int childRow(const NodeIdentifier &p_parentId, const NodeIdentifier &p_childId) const;
  quintptr indexIdForNode(const NodeIdentifier &p_nodeId) const;
  NodeIdentifier nodeIdForIndexId(quintptr p_indexId) const;
  QVector<NodeInfo> parseChildrenFromJson(const QJsonObject &p_json,
                                          const NodeIdentifier &p_parentId) const;
  QVector<NodeInfo> parseExternalNodesFromJson(const QJsonObject &p_json,
                                               const NodeIdentifier &p_parentId) const;
  NodeInfo parseNodeInfoFromJson(const QJsonObject &p_json, const NodeIdentifier &p_parentId) const;
  QIcon getNodeIcon(const NodeInfo &p_info) const;

  ServiceLocator &m_services;
  QString m_notebookId;
  NodeIdentifier m_displayRoot; // Virtual root for flat view modes
  mutable QMap<NodeIdentifier, NodeInfo> m_nodeCache;
  mutable QMap<NodeIdentifier, QVector<NodeIdentifier>> m_childrenCache;
  mutable QSet<NodeIdentifier> m_fetchedNodes;
  mutable QHash<NodeIdentifier, quintptr> m_indexIdCache;
  mutable QHash<quintptr, NodeIdentifier> m_indexIdLookup;
  mutable quintptr m_nextIndexId = 1;
  bool m_externalNodesVisible = false;
};

} // namespace vnotex

#endif // NOTEBOOKNODEMODEL_H
