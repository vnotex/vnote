#ifndef FILENODEDELEGATE_H
#define FILENODEDELEGATE_H

#include <QStyledItemDelegate>

namespace vnotex {

class ServiceLocator;
struct NodeInfo;

// Custom delegate for file nodes in two-column explorer.
// Renders two-line items:
// - Line 1: Icon + File name (bold)
// - Line 2: Preview placeholder + Tag badges (right-aligned)
class FileNodeDelegate : public QStyledItemDelegate {
  Q_OBJECT

public:
  explicit FileNodeDelegate(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~FileNodeDelegate() override;

  void paint(QPainter *p_painter, const QStyleOptionViewItem &p_option,
             const QModelIndex &p_index) const override;

  QSize sizeHint(const QStyleOptionViewItem &p_option,
                 const QModelIndex &p_index) const override;

private:
  // Paint the two-line file node content
  void paintFileNode(QPainter *p_painter, const QStyleOptionViewItem &p_option,
                     const NodeInfo &p_nodeInfo, const QModelIndex &p_index) const;

  // Paint tag badges at bottom-right of item rect
  // Returns the width consumed by tags (for preview text layout)
  int paintTags(QPainter *p_painter, const QRect &p_rect,
                const QStringList &p_tags, const QColor &p_textColor) const;

  // Get background color for a node (from NodeVisual)
  QColor getNodeBackgroundColor(const NodeInfo &p_nodeInfo,
                                const QStyleOptionViewItem &p_option) const;

  // Get text color for a node (from NodeVisual or theme)
  QColor getNodeTextColor(const NodeInfo &p_nodeInfo,
                          const QStyleOptionViewItem &p_option) const;

  // Get icon for file node
  QIcon getNodeIcon(const NodeInfo &p_nodeInfo) const;

  // Configuration
  ServiceLocator &m_services;
  int m_hPadding = 6;   // Horizontal padding (left/right margins)
  int m_vPadding = 10;  // Vertical padding (top/bottom margins)
  int m_iconSize = 16;
  int m_lineSpacing = 2;
  int m_maxVisibleTags = 3;
  int m_maxTagWidth = 60;
};

} // namespace vnotex

#endif // FILENODEDELEGATE_H
