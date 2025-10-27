#ifndef TREEWIDGETITEM_H
#define TREEWIDGETITEM_H

#include <QTreeWidgetItem>

namespace vnotex {
// Provide additional features:
// 1. Sorting case-insensitive.
class TreeWidgetItem : public QTreeWidgetItem {
public:
  TreeWidgetItem(QTreeWidget *p_parent, const QStringList &p_strings, int p_type = Type);

  bool operator<(const QTreeWidgetItem &p_other) const Q_DECL_OVERRIDE;
};
} // namespace vnotex

#endif // TREEWIDGETITEM_H
