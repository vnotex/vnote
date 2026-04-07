#include "treefilterproxymodel.h"

using namespace vnotex;

TreeFilterProxyModel::TreeFilterProxyModel(QObject *p_parent)
    : QSortFilterProxyModel(p_parent) {
  setRecursiveFilteringEnabled(true);
}

TreeFilterProxyModel::~TreeFilterProxyModel() {}

void TreeFilterProxyModel::setFilterText(const QString &p_text) {
  if (m_filterText != p_text) {
    bool wasActive = !m_filterText.isEmpty();
    m_filterText = p_text;
    invalidateFilter();
    bool isActive = !m_filterText.isEmpty();
    if (wasActive != isActive) {
      emit filterActiveChanged(isActive);
    }
  }
}

bool TreeFilterProxyModel::filterAcceptsRow(int p_sourceRow,
                                            const QModelIndex &p_sourceParent) const {
  if (m_filterText.isEmpty()) {
    return true;
  }

  QModelIndex index = sourceModel()->index(p_sourceRow, 0, p_sourceParent);
  if (!index.isValid()) {
    return false;
  }

  QString displayText = index.data(Qt::DisplayRole).toString();
  return displayText.contains(m_filterText, Qt::CaseInsensitive);
}
