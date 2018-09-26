#include "vtreewidget.h"

#include <QDropEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QSet>
#include <QScrollBar>
#include <QGraphicsOpacityEffect>
#include <QTimer>

#include "utils/vutils.h"
#include "utils/vimnavigationforwidget.h"
#include "vstyleditemdelegate.h"

#define SEARCH_INPUT_NORMAL_OPACITY 0.8

#define SEARCH_INPUT_IDLE_OPACITY 0.2

VTreeWidget::VTreeWidget(QWidget *p_parent)
    : QTreeWidget(p_parent),
      ISimpleSearch(),
      m_fitContent(false)
{
    setAttribute(Qt::WA_MacShowFocusRect, false);

    m_searchInput = new VSimpleSearchInput(this, this);
    connect(m_searchInput, &VSimpleSearchInput::triggered,
            this, &VTreeWidget::handleSearchModeTriggered);
    connect(m_searchInput, &VSimpleSearchInput::inputTextChanged,
            this, &VTreeWidget::handleSearchInputTextChanged);

    QGraphicsOpacityEffect * effect = new QGraphicsOpacityEffect(m_searchInput);
    effect->setOpacity(SEARCH_INPUT_NORMAL_OPACITY);
    m_searchInput->setGraphicsEffect(effect);
    m_searchInput->hide();

    m_searchColdTimer = new QTimer(this);
    m_searchColdTimer->setSingleShot(true);
    m_searchColdTimer->setInterval(1000);
    connect(m_searchColdTimer, &QTimer::timeout,
            this, [this]() {
                QGraphicsOpacityEffect *effect = getSearchInputEffect();
                Q_ASSERT(effect);
                effect->setOpacity(SEARCH_INPUT_IDLE_OPACITY);
            });

    m_delegate = new VStyledItemDelegate(NULL, this);
    setItemDelegate(m_delegate);

    m_expandTimer = new QTimer(this);
    m_expandTimer->setSingleShot(true);
    m_expandTimer->setInterval(100);
    connect(m_expandTimer, &QTimer::timeout,
            this, [this]() {
                if (m_fitContent) {
                    resizeColumnToContents(0);
                }

                emit itemExpandedOrCollapsed();
            });

    connect(this, &VTreeWidget::itemExpanded,
            m_expandTimer, static_cast<void(QTimer::*)(void)>(&QTimer::start));
    connect(this, &VTreeWidget::itemCollapsed,
            m_expandTimer, static_cast<void(QTimer::*)(void)>(&QTimer::start));
}

void VTreeWidget::keyPressEvent(QKeyEvent *p_event)
{
    if (m_searchInput->tryHandleKeyPressEvent(p_event)) {
        return;
    }

    if (VimNavigationForWidget::injectKeyPressEventForVim(this, p_event)) {
        return;
    }

    QTreeWidget::keyPressEvent(p_event);
}

void VTreeWidget::clearAll()
{
    m_searchInput->clear();
    setSearchInputVisible(false);

    VTreeWidget::clear();
}

void VTreeWidget::setSearchInputVisible(bool p_visible)
{
    m_searchInput->setVisible(p_visible);
    // setViewportMargins() and setContentsMargins() do not work for QTreeWidget.
    // setStyleSheet(QString("padding-bottom: %1px").arg(bottomMargin));
    QGraphicsOpacityEffect *effect = getSearchInputEffect();
    Q_ASSERT(effect);
    effect->setOpacity(SEARCH_INPUT_NORMAL_OPACITY);
}

void VTreeWidget::resizeEvent(QResizeEvent *p_event)
{
    QTreeWidget::resizeEvent(p_event);

    QRect contentRect = contentsRect();
    int width = contentRect.width();
    QScrollBar *vbar = verticalScrollBar();
    if (vbar && (vbar->minimum() != vbar->maximum())) {
        width -= vbar->width();
    }

    int y = height() - m_searchInput->height();
    QScrollBar *hbar = horizontalScrollBar();
    if (hbar && (hbar->minimum() != hbar->maximum())) {
        y -= hbar->height();
    }

    m_searchInput->setGeometry(QRect(contentRect.left(),
                                     y,
                                     width,
                                     m_searchInput->height()));
}

void VTreeWidget::handleSearchModeTriggered(bool p_inSearchMode, bool p_focus)
{
    setSearchInputVisible(p_inSearchMode);
    if (!p_inSearchMode) {
        clearItemsHighlight();
    }

    if (p_focus) {
        setFocus();
    }
}

void VTreeWidget::dropEvent(QDropEvent *p_event)
{
    QList<QTreeWidgetItem *> dragItems = selectedItems();

    int first = -1, last = -1;
    QTreeWidgetItem *firstItem = NULL;
    for (int i = 0; i < dragItems.size(); ++i) {
        int row = indexFromItem(dragItems[i]).row();
        if (row > last) {
            last = row;
        }

        if (first == -1 || row < first) {
            first = row;
            firstItem = dragItems[i];
        }
    }

    Q_ASSERT(firstItem);

    QTreeWidget::dropEvent(p_event);

    int target = indexFromItem(firstItem).row();
    emit rowsMoved(first, last, target);
}

QList<void *> VTreeWidget::searchItems(const QString &p_text,
                                       Qt::MatchFlags p_flags) const
{
    QList<QTreeWidgetItem *> items = findItems(p_text, p_flags);

    QList<void *> res;
    res.reserve(items.size());
    for (int i = 0; i < items.size(); ++i) {
        if (items[i]->type() == ItemTypeSeparator) {
            continue;
        }

        res.append(items[i]);
    }

    return res;
}

void VTreeWidget::highlightHitItems(const QList<void *> &p_items)
{
    clearItemsHighlight();

    QSet<QModelIndex> hitIndexes;
    for (auto it : p_items) {
        QModelIndex index = indexFromItem(static_cast<QTreeWidgetItem *>(it));
        if (index.isValid()) {
            hitIndexes.insert(index);
        }
    }

    if (!hitIndexes.isEmpty()) {
        m_delegate->setHitItems(hitIndexes);
        update();
    }
}

void VTreeWidget::clearItemsHighlight()
{
    m_delegate->clearHitItems();
    update();
}

void VTreeWidget::selectHitItem(void *p_item)
{
    setCurrentItem(static_cast<QTreeWidgetItem *>(p_item), 0, QItemSelectionModel::ClearAndSelect);
}

// Count the total number of tree @p_item.
static int treeItemCount(QTreeWidgetItem *p_item)
{
    if (!p_item) {
        return 0;
    }

    int child = p_item->childCount();
    int total = 1;
    for (int i = 0; i < child; ++i) {
        total += treeItemCount(p_item->child(i));
    }

    return total;
}

int VTreeWidget::totalNumberOfItems()
{
    int total = 0;
    int cn = topLevelItemCount();
    for (int i = 0; i < cn; ++i) {
        total += treeItemCount(topLevelItem(i));
    }

    return total;
}

void VTreeWidget::handleSearchInputTextChanged(const QString &p_text)
{
    m_searchColdTimer->stop();
    m_searchColdTimer->start();

    Q_UNUSED(p_text);
    QGraphicsOpacityEffect *effect = getSearchInputEffect();
    Q_ASSERT(effect);
    effect->setOpacity(0.8);
}

QGraphicsOpacityEffect *VTreeWidget::getSearchInputEffect() const
{
    return static_cast<QGraphicsOpacityEffect *>(m_searchInput->graphicsEffect());
}

static QTreeWidgetItem *lastItemOfTree(QTreeWidgetItem *p_item)
{
    if (p_item->isExpanded() && p_item->childCount() > 0) {
        return p_item->child(p_item->childCount() - 1);
    } else {
        return p_item;
    }
}

QTreeWidgetItem *VTreeWidget::nextSibling(const QTreeWidget *p_tree,
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

void VTreeWidget::selectNextItem(bool p_forward)
{
    if (topLevelItemCount() == 0) {
        return;
    }

    QTreeWidgetItem *item = currentItem();
    if (!item) {
        setCurrentItem(topLevelItem(0), 0, QItemSelectionModel::ClearAndSelect);
        return;
    }

    QTreeWidgetItem *nItem = nextItem(this, item, p_forward);
    if (nItem) {
        setCurrentItem(nItem, 0, QItemSelectionModel::ClearAndSelect);
    }
}

QTreeWidgetItem *VTreeWidget::nextItem(const QTreeWidget *p_tree,
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

void VTreeWidget::selectParentItem()
{
    QTreeWidgetItem *item = currentItem();
    if (item) {
        QTreeWidgetItem *pitem = item->parent();
        if (pitem) {
            setCurrentItem(pitem, 0, QItemSelectionModel::ClearAndSelect);
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

bool VTreeWidget::isTreeExpanded(const QTreeWidget *p_tree)
{
    int cnt = p_tree->topLevelItemCount();
    for (int i = 0; i < cnt; ++i) {
        if (!isItemTreeExpanded(p_tree->topLevelItem(i))) {
            return false;
        }
    }

    return true;
}

void VTreeWidget::expandCollapseAll(QTreeWidget *p_tree)
{
    QTreeWidgetItem *topLevelItem = NULL;
    QTreeWidgetItem *item = p_tree->currentItem();
    if (item) {
        topLevelItem = const_cast<QTreeWidgetItem *>(topLevelTreeItem(item));
    }

    bool expanded = isTreeExpanded(p_tree);
    if (expanded) {
        p_tree->collapseAll();
    } else {
        p_tree->expandAll();

    }

    VTreeWidget *vtree = dynamic_cast<VTreeWidget *>(p_tree);
    if (vtree) {
        if (vtree->m_fitContent) {
            vtree->resizeColumnToContents(0);
        }

        emit vtree->itemExpandedOrCollapsed();
    }

    if (topLevelItem) {
        p_tree->setCurrentItem(topLevelItem, 0, QItemSelectionModel::ClearAndSelect);
        p_tree->scrollToItem(topLevelItem);
    }
}

const QTreeWidgetItem *VTreeWidget::topLevelTreeItem(const QTreeWidgetItem *p_item)
{
    if (!p_item) {
        return NULL;
    }

    if (p_item->parent()) {
        return topLevelTreeItem(p_item->parent());
    } else {
        return p_item;
    }
}

int VTreeWidget::childIndexOfTreeItem(const QTreeWidgetItem *p_item)
{
    if (p_item->parent()) {
        return p_item->parent()->indexOfChild(const_cast<QTreeWidgetItem *>(p_item));
    } else {
        return 0;
    }
}
