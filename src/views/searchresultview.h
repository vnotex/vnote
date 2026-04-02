#ifndef SEARCHRESULTVIEW_H
#define SEARCHRESULTVIEW_H

#include <QTreeView>

namespace vnotex {

// A QTreeView subclass for displaying search results.
// File-level items are expandable; line-level items are leaves.
// Emits signals for activation and context menu; no business logic.
class SearchResultView : public QTreeView {
  Q_OBJECT

public:
  explicit SearchResultView(QWidget *p_parent = nullptr);
  ~SearchResultView() override;

  // Override to connect modelReset → expandAll
  void setModel(QAbstractItemModel *p_model) override;

signals:
  // Emitted when a result item is activated (double-click or Enter)
  void resultActivated(const QModelIndex &p_index);

  // Emitted when context menu is requested on an item
  void contextMenuRequested(const QModelIndex &p_index, const QPoint &p_globalPos);

protected:
  void mouseDoubleClickEvent(QMouseEvent *p_event) override;
  void keyPressEvent(QKeyEvent *p_event) override;
  void contextMenuEvent(QContextMenuEvent *p_event) override;

private:
  void setupView();
};

} // namespace vnotex

#endif // SEARCHRESULTVIEW_H
