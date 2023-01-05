#include "treewidget.h"

#include <QMouseEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QDropEvent>

#include <utils/widgetutils.h>
#include "styleditemdelegate.h"

using namespace vnotex;

TreeWidget::TreeWidget(QWidget *p_parent)
    : QTreeWidget(p_parent)
{
}

TreeWidget::TreeWidget(TreeWidget::Flags p_flags, QWidget *p_parent)
    : QTreeWidget(p_parent),
      m_flags(p_flags)
{
    if (m_flags & Flag::EnhancedStyle) {
        auto interface = QSharedPointer<StyledItemDelegateTreeWidget>::create(this);
        auto delegate = new StyledItemDelegate(interface, StyledItemDelegate::Highlights, this);
        setItemDelegate(delegate);
    }
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

QTreeWidgetItem *TreeWidget::findItem(const QTreeWidget *p_widget, const QVariant &p_data, int p_column)
{
    int nrTop = p_widget->topLevelItemCount();
    for (int i = 0; i < nrTop; ++i) {
        auto item = findItemHelper(p_widget->topLevelItem(i), p_data, p_column);
        if (item) {
            return item;
        }
    }

    return nullptr;
}

QTreeWidgetItem *TreeWidget::findItemHelper(QTreeWidgetItem *p_item, const QVariant &p_data, int p_column)
{
    if (!p_item) {
        return nullptr;
    }

    if (p_item->data(0, Qt::UserRole) == p_data) {
        return p_item;
    }

    int nrChild = p_item->childCount();
    for (int i = 0; i < nrChild; ++i) {
        auto item = findItemHelper(p_item->child(i), p_data, p_column);
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

    // On Mac OS X, it is `Command+O` to activate an item, instead of Return.
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    if (p_event->key() == Qt::Key_Return || p_event->key() == Qt::Key_Enter) {
        if (auto item = currentItem()) {
            emit itemActivated(item, currentColumn());
        }
        return;
    }
#endif

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

void TreeWidget::dropEvent(QDropEvent *p_event)
{
    auto dragItems = selectedItems();

    QTreeWidget::dropEvent(p_event);

    if (dragItems.size() == 1) {
        emit itemMoved(dragItems[0]);
    }
}

void TreeWidget::forEachItem(const QTreeWidget *p_widget, const std::function<bool(QTreeWidgetItem *p_item)> &p_func)
{
    const int cnt = p_widget->topLevelItemCount();
    for (int i = 0; i < cnt; ++i) {
        if (!forEachItem(p_widget->topLevelItem(i), p_func)) {
            break;
        }
    }
}

bool TreeWidget::forEachItem(QTreeWidgetItem *p_item, const std::function<bool(QTreeWidgetItem *p_item)> &p_func)
{
    if (!p_item) {
        return true;
    }

    if (!p_func(p_item)) {
        return false;
    }

    const int cnt = p_item->childCount();
    for (int i = 0; i < cnt; ++i) {
        if (!forEachItem(p_item->child(i), p_func)) {
            return false;
        }
    }

    return true;
}

void TreeWidget::mark(QTreeWidgetItem *p_item, int p_column)
{
    p_item->setData(p_column, Qt::ForegroundRole, StyledItemDelegate::s_highlightForeground);
    p_item->setData(p_column, Qt::BackgroundRole, StyledItemDelegate::s_highlightBackground);
}

void TreeWidget::unmark(QTreeWidgetItem *p_item, int p_column)
{
    p_item->setData(p_column, Qt::ForegroundRole, QVariant());
    p_item->setData(p_column, Qt::BackgroundRole, QVariant());
}

void TreeWidget::expandRecursively(QTreeWidgetItem *p_item)
{
    if (!p_item) {
        return;
    }

    p_item->setExpanded(true);
    const int cnt = p_item->childCount();
    if (cnt == 0) {
        return;
    }

    for (int i = 0; i < cnt; ++i) {
        expandRecursively(p_item->child(i));
    }
}

void TreeWidget::selectParentItem(QTreeWidget *p_widget)
{
    auto item = p_widget->currentItem();
    if (item) {
        auto pitem = item->parent();
        if (pitem) {
            p_widget->setCurrentItem(pitem, 0, QItemSelectionModel::ClearAndSelect);
        }
    }
}

static bool isItemTreeExpanded(const QTreeWidgetItem *p_item)
{
    if (!p_item) {
        return true;
    }

    if (p_item->isHidden() || !p_item->isExpanded()) {
        return false;
    }

    int cnt = p_item->childCount();
    for (int i = 0; i < cnt; ++i) {
        if (!isItemTreeExpanded(p_item->child(i))) {
            return false;
        }
    }

    return true;
}

bool TreeWidget::isExpanded(const QTreeWidget *p_widget)
{
    int cnt = p_widget->topLevelItemCount();
    for (int i = 0; i < cnt; ++i) {
        if (!isItemTreeExpanded(p_widget->topLevelItem(i))) {
            return false;
        }
    }

    return true;
}
