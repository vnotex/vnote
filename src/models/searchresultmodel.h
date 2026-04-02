#ifndef SEARCHRESULTMODEL_H
#define SEARCHRESULTMODEL_H

#include <QAbstractItemModel>

#include <core/searchresulttypes.h>

namespace vnotex {

class SearchResultModel : public QAbstractItemModel {
  Q_OBJECT

public:
  enum Role {
    NodeIdRole = Qt::UserRole + 1,
    LineNumberRole,
    ColumnStartRole,
    ColumnEndRole,
    IsFileResultRole,
    AbsolutePathRole,
    MatchCountRole,
  };

  explicit SearchResultModel(QObject *p_parent = nullptr);
  ~SearchResultModel() override;

  QModelIndex index(int p_row, int p_column,
                    const QModelIndex &p_parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex &p_child) const override;
  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &p_parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override;

  void setSearchResult(const SearchResult &p_result);
  void clear();
  int totalMatchCount() const;
  bool isTruncated() const;

private:
  SearchResult m_result;
};

} // namespace vnotex

#endif // SEARCHRESULTMODEL_H
