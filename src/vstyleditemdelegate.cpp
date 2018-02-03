#include "vstyleditemdelegate.h"

#include <QPainter>
#include <QDebug>

#include "vpalette.h"

extern VPalette *g_palette;

VStyledItemDelegate::VStyledItemDelegate(QObject *p_parent)
    : QStyledItemDelegate(p_parent)
{
    m_itemHitBg = QBrush(QColor(g_palette->color("search_hit_item_bg")));
    m_itemHitFg = QBrush(QColor(g_palette->color("search_hit_item_fg")));
}

void VStyledItemDelegate::paint(QPainter *p_painter,
                                const QStyleOptionViewItem &p_option,
                                const QModelIndex &p_index) const
{
    if (isHit(p_index)) {
        QStyleOptionViewItem option(p_option);
        p_painter->fillRect(option.rect, m_itemHitBg);
        // Does not work anyway.
        // option.palette.setBrush(QPalette::Base, m_itemHitBg);
        option.palette.setBrush(QPalette::Text, m_itemHitFg);
        QStyledItemDelegate::paint(p_painter, option, p_index);
    } else {
        QStyledItemDelegate::paint(p_painter, p_option, p_index);
    }
}
