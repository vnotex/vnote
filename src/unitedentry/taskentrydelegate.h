#ifndef TASKENTRYDELEGATE_H
#define TASKENTRYDELEGATE_H

#include <QStyledItemDelegate>

namespace vnotex {
class ServiceLocator;

// Renders a row of the "task" united entry on two lines: the task label on the
// first line, and the task's scope path (e.g. "app/git/commit") on a second,
// smaller and dimmed line. Falls back to the default rendering for items that
// carry no path.
class TaskEntryDelegate : public QStyledItemDelegate {
  Q_OBJECT
public:
  // Item data role holding the path string drawn on the second line.
  enum ItemDataRole { PathRole = Qt::UserRole + 1 };

  explicit TaskEntryDelegate(ServiceLocator &p_services, QObject *p_parent = nullptr);

  void paint(QPainter *p_painter, const QStyleOptionViewItem &p_option,
             const QModelIndex &p_index) const Q_DECL_OVERRIDE;

  QSize sizeHint(const QStyleOptionViewItem &p_option,
                 const QModelIndex &p_index) const Q_DECL_OVERRIDE;

private:
  // The (smaller) font used for the path line.
  static QFont pathFont(const QFont &p_baseFont);

  // Resolves the row's foreground color for the current state. The themes set
  // "selection-background-color: transparent" on QTreeView and paint the item
  // background/foreground purely through QSS (widgets#qtreeview#item#...), and
  // those QSS colors never reach QStyleOptionViewItem::palette. So the color
  // must come from ThemeService, exactly as NotebookNodeDelegate does; using
  // palette.highlightedText() yields unreadable text on the selected row.
  QColor resolveTextColor(const QStyleOptionViewItem &p_option) const;

  ServiceLocator &m_services;

  const int m_hPadding = 6;

  const int m_vPadding = 4;

  const int m_lineSpacing = 2;

  const int m_iconSize = 16;
};
} // namespace vnotex

#endif // TASKENTRYDELEGATE_H
