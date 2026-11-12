#ifndef NOTEBOOKNODEDELEGATE_H
#define NOTEBOOKNODEDELEGATE_H

#include <gui/utils/treechildcountdelegate.h>

#include <core/nodeinfo.h>
#include <core/servicelocator.h>

namespace vnotex {

// A QStyledItemDelegate for custom rendering of notebook nodes.
// Handles visual styling including:
// - Node icons (folder/file)
// - Node name with optional styling (colors, badges)
// - Selection highlighting
// - Drag feedback
class NotebookNodeDelegate : public TreeChildCountDelegate {
  Q_OBJECT

public:
  explicit NotebookNodeDelegate(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~NotebookNodeDelegate() override;

  void paint(QPainter *p_painter, const QStyleOptionViewItem &p_option,
             const QModelIndex &p_index) const override;

  QSize sizeHint(const QStyleOptionViewItem &p_option, const QModelIndex &p_index) const override;

  // Override to select only base name (without extension) when editing starts
  void setEditorData(QWidget *p_editor, const QModelIndex &p_index) const override;

private:
  // Paint the node content
  void paintNode(QPainter *p_painter, const QStyleOptionViewItem &p_option,
                 const NodeInfo &p_nodeInfo) const;

  // Get background color for a node (from NodeVisual)
  QColor getNodeBackgroundColor(const NodeInfo &p_nodeInfo,
                                const QStyleOptionViewItem &p_option) const;

  // Get text color for a node (from NodeVisual)
  QColor getNodeTextColor(const NodeInfo &p_nodeInfo, const QStyleOptionViewItem &p_option) const;

  // Configuration
  ServiceLocator &m_services;
  const int m_iconSize = 16;
};

} // namespace vnotex

#endif // NOTEBOOKNODEDELEGATE_H
