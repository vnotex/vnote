#include "vlistwidgetdoublerows.h"

#include <QListWidgetItem>
#include <QScrollBar>
#include <QDebug>

#include "vdoublerowitemwidget.h"

VListWidgetDoubleRows::VListWidgetDoubleRows(QWidget *p_parent)
    : VListWidget(p_parent)
{
}

QListWidgetItem *VListWidgetDoubleRows::addDoubleRowsItem(const QIcon &p_icon,
                                                          const QString &p_firstRow,
                                                          const QString &p_secondRow)
{
    return insertDoubleRowsItem(count(),
                                p_icon,
                                p_firstRow,
                                p_secondRow);
}

QListWidgetItem *VListWidgetDoubleRows::insertDoubleRowsItem(int p_row,
                                                             const QIcon &p_icon,
                                                             const QString &p_firstRow,
                                                             const QString &p_secondRow)
{
    VDoubleRowItemWidget *itemWidget = new VDoubleRowItemWidget(this);
    itemWidget->setText(p_firstRow, p_secondRow);
    QSize sz = itemWidget->sizeHint();
    QSize iconSz = iconSize();
    if (!iconSz.isValid()) {
        iconSz = QSize(sz.height(), sz.height());
        setIconSize(iconSz);
    }

    sz.setHeight(sz.height() * 1.25);

    QListWidgetItem *item = new QListWidgetItem();
    if (!p_icon.isNull()) {
        item->setIcon(p_icon);
        sz.setWidth(sz.width() + iconSz.width());
        sz.setHeight(qMax(sz.height(), iconSz.height()));
    }

    item->setSizeHint(sz);

    insertItem(p_row, item);
    setItemWidget(item, itemWidget);
    return item;
}

void VListWidgetDoubleRows::clearAll()
{
    // Delete the item widget for each item.
    int cnt = count();
    for (int i = 0; i < cnt; ++i) {
        QListWidgetItem *it = item(i);
        QWidget *wid = itemWidget(it);
        removeItemWidget(it);
        delete wid;
    }

    VListWidget::clearAll();

    setIconSize(QSize());
}

void VListWidgetDoubleRows::moveItem(int p_srcRow, int p_destRow)
{
    QListWidgetItem *it = item(p_srcRow);
    QWidget *wid = itemWidget(it);

    takeItem(p_srcRow);
    insertItem(p_destRow, it);

    if (wid) {
        QWidget *newWid = VDoubleRowItemWidget::cloneWidget(static_cast<VDoubleRowItemWidget *>(wid), this);
        removeItemWidget(it);
        delete wid;

        setItemWidget(it, newWid);
    }
}
