#ifndef TREEFILTERPROXYMODEL_H
#define TREEFILTERPROXYMODEL_H

#include <QSortFilterProxyModel>

namespace vnotex {

// A reusable QSortFilterProxyModel for case-insensitive recursive tree filtering.
// Accepts rows whose Qt::DisplayRole contains the filter text (case-insensitive).
// When filter text is empty, all rows are accepted.
class TreeFilterProxyModel : public QSortFilterProxyModel {
  Q_OBJECT

public:
  explicit TreeFilterProxyModel(QObject *p_parent = nullptr);
  ~TreeFilterProxyModel() override;

signals:
  void filterActiveChanged(bool p_active);

public slots:
  void setFilterText(const QString &p_text);

protected:
  bool filterAcceptsRow(int p_sourceRow, const QModelIndex &p_sourceParent) const override;

private:
  QString m_filterText;
};

} // namespace vnotex

#endif // TREEFILTERPROXYMODEL_H
