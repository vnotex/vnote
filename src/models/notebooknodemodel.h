#ifndef NOTEBOOKNODEMODEL_H
#define NOTEBOOKNODEMODEL_H

#include <QAbstractItemModel>
#include <QIcon>
#include <QSet>
#include <QSharedPointer>

#include <core/nodeinfo.h>

namespace vnotex {

class Node;
class Notebook;

// A QAbstractItemModel implementation that exposes Notebook/Node hierarchy
// to Qt's Model/View framework.
class NotebookNodeModel : public QAbstractItemModel {
  Q_OBJECT

public:
  // Custom data roles for node information
  enum Roles {
    // New architecture roles (preferred)
    NodeInfoRole = Qt::UserRole + 1, // NodeInfo struct
    IsFolderRole,                     // bool - is folder

    // Legacy roles (for backward compatibility during migration)
    NodeRole,         // Node* pointer (DEPRECATED: use NodeInfoRole)
    NodeTypeRole,     // Node::Flags
    IsContainerRole,  // bool - is folder (DEPRECATED: use IsFolderRole)
    ChildCountRole,   // int - number of children
    PathRole,         // QString - node path
    ModifiedTimeRole, // QDateTime
    CreatedTimeRole   // QDateTime
  };

  explicit NotebookNodeModel(QObject *p_parent = nullptr);
  ~NotebookNodeModel() override;

  // Core QAbstractItemModel interface
  QModelIndex index(int p_row, int p_column,
                    const QModelIndex &p_parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex &p_child) const override;
  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &p_parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex &p_index) const override;

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
  void setNotebook(const QSharedPointer<Notebook> &p_notebook);
  QSharedPointer<Notebook> getNotebook() const;

  // Node <-> Index conversion
  Node *nodeFromIndex(const QModelIndex &p_index) const;
  QModelIndex indexFromNode(Node *p_node) const;

  // Reload operations
  void reload();
  void reloadNode(Node *p_node);

  // Notify model about external changes to a node
  void nodeDataChanged(Node *p_node);

  // Convert Node* to NodeInfo struct
  NodeInfo nodeToNodeInfo(Node *p_node) const;
signals:
  void notebookChanged();

private:
  // Helper to get children from a node (handles root specially)
  QVector<QSharedPointer<Node>> getNodeChildren(Node *p_node) const;

  // Check if a node's children have been loaded into the model
  bool isNodeFetched(Node *p_node) const;
  void markNodeFetched(Node *p_node);

  // Get icon for a node based on its type
  QIcon getNodeIcon(Node *p_node) const;


  // The current notebook
  QSharedPointer<Notebook> m_notebook;

  // Track which nodes have been fetched (for lazy loading)
  mutable QSet<Node *> m_fetchedNodes;
};

} // namespace vnotex

#endif // NOTEBOOKNODEMODEL_H
