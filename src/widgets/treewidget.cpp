#include "treewidget.h"

#include <QMouseEvent>
#include <QHeaderView>
#include <QKeyEvent>

#include <utils/widgetutils.h>

using namespace vnotex;

TreeWidget::TreeWidget(QWidget *p_parent)
    : QTreeWidget(p_parent)
{
}

TreeWidget::TreeWidget(TreeWidget::Flags p_flags, QWidget *p_parent)
    : QTreeWidget(p_parent),
      m_flags(p_flags)
{
}

void TreeWidget::mousePressEvent(QMouseEvent *p_event)
{
    QTreeWidget::mousePressEvent(p_event);

    if (m_flags & Flag::ClickSpaceToClearSelection) {
        auto idx = indexAt(p_event->pos());
        if (!idx.isValid()) {
            clearSelection();
            setCurrentItem(NULL);
        }
    }
}

void TreeWidget::setupSingleColumnHeaderlessTree(QTreeWidget *p_widget, bool p_contextMenu, bool p_extendedSelection)
{
    p_widget->setColumnCount(1);
    p_widget->setHeaderHidden(true);
    if (p_contextMenu) {
        p_widget->setContextMenuPolicy(Qt::CustomContextMenu);
    }
    if (p_extendedSelection) {
        p_widget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    }
}

void TreeWidget::showHorizontalScrollbar(QTreeWidget *p_tree)
{
    p_tree->header()->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    p_tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    p_tree->header()->setStretchLastSection(false);
}

QTreeWidgetItem *TreeWidget::findItem(const QTreeWidget *p_widget, const QVariant &p_data)
{
    int nrTop = p_widget->topLevelItemCount();
    for (int i = 0; i < nrTop; ++i) {
        auto item = findItemHelper(p_widget->topLevelItem(i), p_data);
        if (item) {
            return item;
        }
    }

    return nullptr;
}

QTreeWidgetItem *TreeWidget::findItemHelper(QTreeWidgetItem *p_item, const QVariant &p_data)
{
    if (!p_item) {
        return nullptr;
    }

    if (p_item->data(0, Qt::UserRole) == p_data) {
        return p_item;
    }

    int nrChild = p_item->childCount();
    for (int i = 0; i < nrChild; ++i) {
        auto item = findItemHelper(p_item->child(i), p_data);
        if (item) {
            return item;
        }
    }

    return nullptr;
}

void TreeWidget::keyPressEvent(QKeyEvent *p_event)
{
    if (WidgetUtils::processKeyEventLikeVi(this, p_event)) {
        return;
    }

    switch (p_event->key()) {
    case Qt::Key_Return:
        Q_FALLTHROUGH();
    case Qt::Key_Enter:
    {
        auto item = currentItem();
        if (item && item->childCount() > 0) {
            item->setExpanded(!item->isExpanded());
        }

        break;
    }

    default:
        break;
    }

    QTreeWidget::keyPressEvent(p_event);
}

QTreeWidgetItem *TreeWidget::nextItem(const QTreeWidget *p_tree,
                                      QTreeWidgetItem *p_item,
                                      bool p_forward)
{
    QTreeWidgetItem *nItem = NULL;
    if (p_forward) {
        if (p_item->isExpanded() && p_item->childCount() > 0) {
            nItem = p_item->child(0);
        } else {
            while (!nItem && p_item) {
                nItem = nextSibling(p_tree, p_item, true);
                p_item = p_item->parent();
            }
        }
    } else {
        nItem = nextSibling(p_tree, p_item, false);
        if (!nItem) {
            nItem = p_item->parent();
        } else {
            nItem = lastItemOfTree(nItem);
        }
    }

    return nItem;
}

QTreeWidgetItem *TreeWidget::lastItemOfTree(QTreeWidgetItem *p_item)
{
    if (p_item->isExpanded() && p_item->childCount() > 0) {
        return p_item->child(p_item->childCount() - 1);
    } else {
        return p_item;
    }
}

QTreeWidgetItem *TreeWidget::nextSibling(const QTreeWidget *p_tree,
                                         QTreeWidgetItem *p_item,
                                         bool p_forward)
{
    if (!p_item) {
        return NULL;
    }

    QTreeWidgetItem *pa = p_item->parent();
    if (pa) {
        int idx = pa->indexOfChild(p_item);
        if (p_forward) {
            ++idx;
            if (idx >= pa->childCount()) {
                return NULL;
            }
        } else {
            --idx;
            if (idx < 0) {
                return NULL;
            }
        }

        return pa->child(idx);
    } else {
        // Top level item.
        int idx = p_tree->indexOfTopLevelItem(p_item);
        if (p_forward) {
            ++idx;
            if (idx >= p_tree->topLevelItemCount()) {
                return NULL;
            }
        } else {
            --idx;
            if (idx < 0) {
                return NULL;
            }
        }

        return p_tree->topLevelItem(idx);
    }
}

QVector<QTreeWidgetItem *> TreeWidget::getVisibleItems(const QTreeWidget *p_widget)
{
    QVector<QTreeWidgetItem *> items;

    auto firstItem = p_widget->itemAt(0, 0);
    if (!firstItem) {
        return items;
    }

    auto lastItem = p_widget->itemAt(p_widget->viewport()->rect().bottomLeft());

    auto item = firstItem;
    while (item) {
        items.append(item);
        if (item == lastItem) {
            break;
        }

        item = nextItem(p_widget, item, true);
    }

    return items;
}
