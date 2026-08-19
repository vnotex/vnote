#ifndef SORTDIALOG2_H
#define SORTDIALOG2_H

#include <QCollator>
#include <QDateTime>
#include <QDialog>
#include <QStringList>
#include <QVector>

class QTreeWidget;
class QPushButton;

namespace vnotex {

// SortDialog2 — pure-UI modal dialog for reordering a flat list of entries.
//
// Designed for new-architecture (`2` suffix) callers. The caller passes one
// list at a time (either folders OR files; strict separation per the
// notebook-explorer-drag-reorder locked decision). The dialog returns the
// final order via getSortedOrder() after exec() == Accepted.
//
// Rows are shown in three columns (Name / Created / Modified). Clicking a
// header performs a ONE-SHOT reorder (toggling asc/desc on repeat clicks of
// the same column); there is deliberately no live sorting, because the dialog
// authors a manual order and a sort model would silently fight the move
// buttons.
//
// This class has NO ServiceLocator / vxcore dependency by contract; persistence
// is the caller's responsibility (the controller fires hooks + updates vxcore).
class SortDialog2 : public QDialog {
  Q_OBJECT
public:
  // One row of the dialog. The timestamps may be invalid (unknown), in which
  // case the corresponding cell renders empty and sorts last.
  struct Entry {
    QString m_name;
    QDateTime m_createdUtc;
    QDateTime m_modifiedUtc;
  };

  SortDialog2(const QString &p_title, const QString &p_subtitle, const QVector<Entry> &p_entries,
              QWidget *p_parent = nullptr);

  // Valid only after exec() == QDialog::Accepted. Returns the names in their
  // display order top-to-bottom.
  QStringList getSortedOrder() const;

private:
  void setupUi(const QString &p_title, const QString &p_subtitle);

  void moveToTop();
  void moveUp();
  void moveDown();
  void moveToBottom();

  // One-shot reorder driven by a header click.
  void sortByColumn(int p_column);

  // Drop the sort indicator; called whenever the order is edited manually
  // (drag-drop or a move button), never from the header-sort path.
  void clearSortIndicator();

  // Recompute enabled state of the four move buttons based on the current
  // selection. Disables all four when the selection is empty or non-contiguous;
  // disables Top/Up when the contiguous block starts at row 0; disables
  // Down/Bottom when the block ends at the last row.
  void updateButtonsEnabled();

  // Compute [first, last] row range of the current selection. Returns
  // {-1, -1} if the selection is empty. last >= first on success.
  void selectedRowRange(int &p_first, int &p_last) const;

  // Number of rows currently selected (row semantics, not cell semantics).
  int selectedRowCount() const;

  QTreeWidget *m_treeWidget = nullptr;
  QPushButton *m_topBtn = nullptr;
  QPushButton *m_upBtn = nullptr;
  QPushButton *m_downBtn = nullptr;
  QPushButton *m_bottomBtn = nullptr;

  int m_lastSortColumn = -1;
  Qt::SortOrder m_lastSortOrder = Qt::AscendingOrder;

  // Built once: constructing a QCollator inside a comparator is a serious
  // performance trap.
  QCollator m_collator;
};

} // namespace vnotex

#endif // SORTDIALOG2_H
