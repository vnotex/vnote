#ifndef SEARCHRESULTDELEGATE_H
#define SEARCHRESULTDELEGATE_H

#include <QStyledItemDelegate>

namespace vnotex {

// A QStyledItemDelegate for rendering search result items.
// File-level items: bold filename with match count badge.
// Line-level items: dimmed line number prefix, normal text, highlighted match segment.
class SearchResultDelegate : public QStyledItemDelegate {
  Q_OBJECT

public:
  explicit SearchResultDelegate(QWidget *p_parent = nullptr);
  ~SearchResultDelegate() override;

  void paint(QPainter *p_painter, const QStyleOptionViewItem &p_option,
             const QModelIndex &p_index) const override;

  QSize sizeHint(const QStyleOptionViewItem &p_option,
                 const QModelIndex &p_index) const override;

private:
  // Paint a file-level result row (bold name + match count badge)
  void paintFileResult(QPainter *p_painter, const QStyleOptionViewItem &p_option,
                       const QModelIndex &p_index) const;

  // Paint a line-level result row (line number + text with highlighted match)
  void paintLineResult(QPainter *p_painter, const QStyleOptionViewItem &p_option,
                       const QModelIndex &p_index) const;

  const int m_hPadding = 6;
  const int m_vPadding = 3;
};

} // namespace vnotex

#endif // SEARCHRESULTDELEGATE_H
