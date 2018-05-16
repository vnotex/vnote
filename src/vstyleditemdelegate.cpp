#include "vstyleditemdelegate.h"

#include <QPainter>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTreeWidgetItem>

#include "vpalette.h"
#include "vtreewidget.h"

extern VPalette *g_palette;

VStyledItemDelegate::VStyledItemDelegate(QListWidget *p_list, VTreeWidget *p_tree)
    : QStyledItemDelegate(p_list ? (QObject *)p_list : (QObject *)p_tree),
      m_list(p_list),
      m_tree(p_tree)
{
    Q_ASSERT(!(m_list && m_tree));
    m_itemHitBg = QBrush(QColor(g_palette->color("search_hit_item_bg")));
    m_itemHitFg = QBrush(QColor(g_palette->color("search_hit_item_fg")));
    m_itemSeparatorBg = QBrush(QColor(g_palette->color("item_separator_bg")));
    m_itemSeparatorFg = QBrush(QColor(g_palette->color("item_separator_fg")));
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
    } else if (itemType(p_index) == ItemTypeSeparator) {
        QStyleOptionViewItem option(p_option);
        p_painter->fillRect(option.rect, m_itemSeparatorBg);
        option.palette.setBrush(QPalette::Text, m_itemSeparatorFg);
        QStyledItemDelegate::paint(p_painter, option, p_index);
    } else {
        QStyledItemDelegate::paint(p_painter, p_option, p_index);
    }
}

int VStyledItemDelegate::itemType(const QModelIndex &p_index) const
{
    int type = 0;
    if (m_list) {
        QListWidgetItem *item = m_list->item(p_index.row());
        if (item) {
            type = item->type();
        }
    } else if (m_tree) {
        QTreeWidgetItem *item = m_tree->getItemFromIndex(p_index);
        if (item) {
            type = item->type();
        }
    }

    return type;
}
