#ifndef VSTYLEDITEMDELEGATE_H
#define VSTYLEDITEMDELEGATE_H

#include <QStyledItemDelegate>
#include <QBrush>
#include <QSet>


class VStyledItemDelegate : public QStyledItemDelegate
{
public:
    explicit VStyledItemDelegate(QObject *p_parent = Q_NULLPTR);

    virtual void paint(QPainter *p_painter,
                       const QStyleOptionViewItem &p_option,
                       const QModelIndex &p_index) const Q_DECL_OVERRIDE;

    void setHitItems(const QSet<QModelIndex> &p_hitItems);

    void clearHitItems();

private:
    bool isHit(const QModelIndex &p_index) const;

    QBrush m_itemHitBg;

    QBrush m_itemHitFg;

    QSet<QModelIndex> m_hitItems;
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
