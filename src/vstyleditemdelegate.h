#ifndef VSTYLEDITEMDELEGATE_H
#define VSTYLEDITEMDELEGATE_H

#include <QStyledItemDelegate>
#include <QBrush>
#include <QSet>

class QListWidget;
class VTreeWidget;

// Should be larger then QListWidgetItem::UserType and QTreeWidgetItem::UserType.
#define ItemTypeSeparator 2000

class VStyledItemDelegate : public QStyledItemDelegate
{
public:
    explicit VStyledItemDelegate(QListWidget *p_list = nullptr, VTreeWidget *p_tree = nullptr);

    virtual void paint(QPainter *p_painter,
                       const QStyleOptionViewItem &p_option,
                       const QModelIndex &p_index) const Q_DECL_OVERRIDE;

    void setHitItems(const QSet<QModelIndex> &p_hitItems);

    void clearHitItems();

private:
    bool isHit(const QModelIndex &p_index) const;

    int itemType(const QModelIndex &p_index) const;

    QBrush m_itemHitBg;
    QBrush m_itemHitFg;

    QBrush m_itemSeparatorBg;
    QBrush m_itemSeparatorFg;

    QSet<QModelIndex> m_hitItems;

    // m_list OR m_tree (but not both) could be not NULL.
    const QListWidget *m_list;
    const VTreeWidget *m_tree;
};

inline void VStyledItemDelegate::setHitItems(const QSet<QModelIndex> &p_hitItems)
{
    m_hitItems = p_hitItems;
}

inline void VStyledItemDelegate::clearHitItems()
{
    m_hitItems.clear();
}

inline bool VStyledItemDelegate::isHit(const QModelIndex &p_index) const
{
    if (m_hitItems.isEmpty()) {
        return false;
    }

    return m_hitItems.contains(p_index);
}
#endif // VSTYLEDITEMDELEGATE_H
