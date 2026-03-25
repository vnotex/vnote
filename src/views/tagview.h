#ifndef TAGVIEW_H
#define TAGVIEW_H

#include <QStringList>
#include <QTreeView>

namespace vnotex {

// A QTreeView subclass for displaying tag hierarchy.
// Pure view — no service access, no business logic.
class TagView : public QTreeView {
  Q_OBJECT

public:
  explicit TagView(QWidget *p_parent = nullptr);
  ~TagView() override = default;

  // Get tag name from model index via TagModel::TagNameRole
  QString tagNameFromIndex(const QModelIndex &p_index) const;

  // Get all currently selected tag names
  QStringList selectedTagNames() const;

signals:
  // Emitted on double-click or Enter
  void tagActivated(const QString &p_tagName);

  // Emitted when selection changes
  void tagsSelectionChanged(const QStringList &p_selectedTags);

  // Emitted on right-click
  void contextMenuRequested(const QString &p_tagName, const QPoint &p_globalPos);

protected:
  void selectionChanged(const QItemSelection &p_selected,
                        const QItemSelection &p_deselected) override;

private slots:
  void onItemActivated(const QModelIndex &p_index);
  void onContextMenuRequested(const QPoint &p_pos);

private:
  void setupView();
};

} // namespace vnotex

#endif // TAGVIEW_H
