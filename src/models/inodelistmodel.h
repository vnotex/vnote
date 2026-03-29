#ifndef INODELISTMODEL_H
#define INODELISTMODEL_H

#include <QModelIndex>
#include <QString>
#include <QVariant>

#include <core/nodeidentifier.h>
#include <core/nodeinfo.h>

namespace vnotex {

class INodeListModel {
public:
  virtual ~INodeListModel() = default;

  // Shared roles for all node list models.
  // Values MUST match what NotebookNodeModel previously used.
  enum NodeListRoles {
    NodeInfoRole = Qt::UserRole + 1, // NodeInfo struct via QVariant (257)
    IsFolderRole,                    // bool (258)
    NodeIdentifierRole,              // NodeIdentifier struct via QVariant (259)
    IsExternalRole,                  // bool (260)
    ChildCountRole,                  // int (261)
    PathRole,                        // QString (262)
    ModifiedTimeRole,                // QDateTime (263)
    CreatedTimeRole,                 // QDateTime (264)
    PreviewRole                      // QString (265)
  };

  // Node accessor pure virtuals.
  virtual NodeIdentifier nodeIdFromIndex(const QModelIndex &p_index) const = 0;
  virtual NodeInfo nodeInfoFromIndex(const QModelIndex &p_index) const = 0;
  virtual QModelIndex indexFromNodeId(const NodeIdentifier &p_nodeId) const = 0;

  // Capability query virtual methods (defaults return false).
  virtual bool supportsDragDrop() const { return false; }
  virtual bool supportsPreview() const { return false; }
  virtual bool supportsHierarchy() const { return false; }
  virtual bool supportsExternalNodes() const { return false; }
  virtual bool supportsDisplayRoot() const { return false; }

  // Optional display root (default no-op).
  virtual NodeIdentifier getDisplayRoot() const { return NodeIdentifier(); }
  virtual QString getNotebookId() const { return QString(); }
};

} // namespace vnotex

#endif // INODELISTMODEL_H
