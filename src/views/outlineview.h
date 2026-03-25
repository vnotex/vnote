#ifndef OUTLINEVIEW_H
#define OUTLINEVIEW_H

#include <QTreeView>

namespace vnotex {

// A QTreeView subclass for displaying the outline (table of contents) tree.
// Display-only: reads heading data from OutlineModel and emits signals when
// the user activates a heading. Does not contain business logic.
class OutlineView : public QTreeView {
  Q_OBJECT

public:
  explicit OutlineView(QWidget *p_parent = nullptr);

  // Override to reconnect selection model signals after model change.
  void setModel(QAbstractItemModel *p_model) override;

  // Highlight the heading at the given index in the model.
  // Selects the corresponding tree item without emitting headingActivated.
  void highlightHeading(int p_headingIndex);

  // Expand/collapse tree to a given heading level.
  // p_level is 1-based (1 = collapse all, 6 = expand all).
  // p_baseLevel is the level of the first heading in the outline.
  void expandToLevel(int p_level, int p_baseLevel);

signals:
  // Emitted when user clicks/activates a heading item.
  // p_headingIndex is the original index in the flat Outline::m_headings vector.
  void headingActivated(int p_headingIndex);

private:
  // Called when the current item changes (keyboard navigation).
  void handleCurrentChanged(const QModelIndex &p_current);

  // Called when an item is clicked.
  void handleClicked(const QModelIndex &p_index);

  // Read HeadingIndexRole from a model index and emit headingActivated if valid.
  void activateHeading(const QModelIndex &p_index);

  // Setup initial view configuration.
  void setupView();

  // When true, headingActivated emission is suppressed (prevents feedback loops
  // during programmatic selection in highlightHeading).
  bool m_muted = false;
};

} // namespace vnotex

#endif // OUTLINEVIEW_H
