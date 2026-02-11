#ifndef NOTEBOOKNODEPROXYMODEL_H
#define NOTEBOOKNODEPROXYMODEL_H

#include <QSortFilterProxyModel>

namespace vnotex {

class Node;

// A QSortFilterProxyModel for sorting and filtering notebook nodes.
// Supports filtering by node type (folder/note) and name pattern.
class NotebookNodeProxyModel : public QSortFilterProxyModel {
  Q_OBJECT

public:
  // Filter flags to control which nodes are shown
  enum FilterFlag {
    ShowFolders = 0x01,
    ShowNotes = 0x02,
    ShowAll = ShowFolders | ShowNotes
  };
  Q_DECLARE_FLAGS(FilterFlags, FilterFlag)

  explicit NotebookNodeProxyModel(QObject *p_parent = nullptr);
  ~NotebookNodeProxyModel() override;

  // Set which node types to show
  void setFilterFlags(FilterFlags p_flags);
  FilterFlags filterFlags() const;

  // Set name filter pattern (supports wildcards)
  void setNameFilter(const QString &p_pattern);
  QString nameFilter() const;

  // Helper to get Node from proxy index
  Node *nodeFromIndex(const QModelIndex &p_index) const;

protected:
  bool filterAcceptsRow(int p_sourceRow, const QModelIndex &p_sourceParent) const override;
  bool lessThan(const QModelIndex &p_left, const QModelIndex &p_right) const override;

private:
  FilterFlags m_filterFlags = ShowAll;
  QString m_nameFilter;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(NotebookNodeProxyModel::FilterFlags)

} // namespace vnotex

#endif // NOTEBOOKNODEPROXYMODEL_H
