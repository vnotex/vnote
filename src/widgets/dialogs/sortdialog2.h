#ifndef SORTDIALOG2_H
#define SORTDIALOG2_H

#include <QDialog>
#include <QStringList>

class QListWidget;
class QPushButton;

namespace vnotex {

// SortDialog2 — pure-UI modal dialog for reordering a flat list of names.
//
// Designed for new-architecture (`2` suffix) callers. The caller passes one
// list at a time (either folders OR files; strict separation per the
// notebook-explorer-drag-reorder locked decision). The dialog returns the
// final order via getSortedOrder() after exec() == Accepted.
//
// This class has NO ServiceLocator / vxcore dependency by contract; persistence
// is the caller's responsibility (the controller fires hooks + updates vxcore).
class SortDialog2 : public QDialog {
  Q_OBJECT
public:
  SortDialog2(const QString &p_title, const QString &p_subtitle, const QStringList &p_initialOrder,
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

  // Recompute enabled state of the four move buttons based on the current
  // selection. Disables all four when the selection is empty or non-contiguous;
  // disables Top/Up when the contiguous block starts at row 0; disables
  // Down/Bottom when the block ends at the last row.
  void updateButtonsEnabled();

  // Compute [first, last] row range of the current selection. Returns
  // {-1, -1} if the selection is empty. last >= first on success.
  void selectedRowRange(int &p_first, int &p_last) const;

  QListWidget *m_listWidget = nullptr;
  QPushButton *m_topBtn = nullptr;
  QPushButton *m_upBtn = nullptr;
  QPushButton *m_downBtn = nullptr;
  QPushButton *m_bottomBtn = nullptr;
};

} // namespace vnotex

#endif // SORTDIALOG2_H
