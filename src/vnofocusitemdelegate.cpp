#include "vnofocusitemdelegate.h"

VNoFocusItemDelegate::VNoFocusItemDelegate(QWidget *parent)
    : QStyledItemDelegate(parent)
{

}

void VNoFocusItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    QStyleOptionViewItem itemOp(option);
    if (itemOp.state & QStyle::State_HasFocus) {
        itemOp.state ^= QStyle::State_HasFocus;
    }
    QStyledItemDelegate::paint(painter, itemOp, index);
}
