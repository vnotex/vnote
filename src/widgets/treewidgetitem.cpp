#include "treewidgetitem.h"

#include <QTreeWidget>
#include <QVariant>

#include <core/global.h>

using namespace vnotex;

TreeWidgetItem::TreeWidgetItem(QTreeWidget *p_parent, const QStringList &p_strings, int p_type)
    : QTreeWidgetItem(p_parent, p_strings, p_type)
{
}

bool TreeWidgetItem::operator<(const QTreeWidgetItem &p_other) const
{
    int column = treeWidget() ? treeWidget()->sortColumn() : 0;

    // Check ComparisonRole first.
    QVariant v1 = data(column, Role::ComparisonRole);
    QVariant v2 = p_other.data(column, Role::ComparisonRole);
    if (v1.isNull() || v2.isNull()) {
        v1 = data(column, Qt::DisplayRole);
        v2 = p_other.data(column, Qt::DisplayRole);
    }

    if (v1.canConvert<QString>() && v2.canConvert<QString>()) {
        const auto s1 = v1.toString().toLower();
        const auto s2 = v2.toString().toLower();
        return s1 < s2;
    }

    return QTreeWidgetItem::operator<(p_other);
}
