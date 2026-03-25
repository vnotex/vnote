#ifndef TAGNODELISTVIEW_H
#define TAGNODELISTVIEW_H

#include <QListView>

#include <core/nodeidentifier.h>

class QStandardItemModel;

namespace vnotex {

// A QListView subclass for displaying nodes matching selected tags.
// Pure view — no service access, no business logic.
class TagNodeListView : public QListView {
  Q_OBJECT

public:
  enum Roles {
    NotebookIdRole = Qt::UserRole + 1,
    RelativePathRole
  };

  explicit TagNodeListView(QWidget *p_parent = nullptr);
  ~TagNodeListView() override = default;

  // Clear model and populate with nodes from JSON array.
  // Each JSON object has "path" and "notebookId" keys.
  void setNodes(const QJsonArray &p_nodes);

  // Extract NodeIdentifier from model index
  NodeIdentifier nodeIdFromIndex(const QModelIndex &p_index) const;

signals:
  // Emitted on double-click or Enter
  void nodeActivated(const vnotex::NodeIdentifier &p_nodeId);

  // Emitted on right-click
  void contextMenuRequested(const vnotex::NodeIdentifier &p_nodeId, const QPoint &p_globalPos);

private slots:
  void onItemActivated(const QModelIndex &p_index);
  void onContextMenuRequested(const QPoint &p_pos);

private:
  void setupView();

  QStandardItemModel *m_model = nullptr;
};

} // namespace vnotex

#endif // TAGNODELISTVIEW_H
