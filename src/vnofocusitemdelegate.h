#ifndef VNOFOCUSITEMDELEGATE_H
#define VNOFOCUSITEMDELEGATE_H

#include <QStyledItemDelegate>

class VNoFocusItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit VNoFocusItemDelegate(QWidget *parent = 0);
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const Q_DECL_OVERRIDE;
};

#endif // VNOFOCUSITEMDELEGATE_H
