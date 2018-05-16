#include "vsearchresulttree.h"

#include <QAction>
#include <QMenu>

#include "utils/vutils.h"
#include "utils/viconutils.h"
#include "vnote.h"
#include "vmainwindow.h"
#include "vnotefile.h"
#include "vcart.h"
#include "vhistorylist.h"

extern VNote *g_vnote;

extern VMainWindow *g_mainWin;

VSearchResultTree::VSearchResultTree(QWidget *p_parent)
    : VTreeWidget(p_parent)
{
    setColumnCount(1);
    setHeaderHidden(true);
    setExpandsOnDoubleClick(false);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setSelectionMode(QAbstractItemView::ExtendedSelection);

    setSimpleSearchMatchFlags(getSimpleSearchMatchFlags() & ~Qt::MatchRecursive);

    m_noteIcon = VIconUtils::treeViewIcon(":/resources/icons/note_item.svg");
    m_folderIcon = VIconUtils::treeViewIcon(":/resources/icons/dir_item.svg");
    m_notebookIcon = VIconUtils::treeViewIcon(":/resources/icons/notebook_item.svg");

    initActions();

    connect(this, &VTreeWidget::itemActivated,
            this, &VSearchResultTree::activateItem);
    connect(this, &VTreeWidget::customContextMenuRequested,
            this, &VSearchResultTree::handleContextMenuRequested);
}

void VSearchResultTree::initActions()
{
    m_openAct = new QAction(tr("&Open"), this);
    m_openAct->setToolTip(tr("Open selected notes"));
    connect(m_openAct, &QAction::triggered,
            this, [this]() {
                activateItem(currentItem());
            });

    m_locateAct = new QAction(VIconUtils::menuIcon(":/resources/icons/locate_note.svg"),
                              tr("&Locate To Folder"),
                              this);
    m_locateAct->setToolTip(tr("Locate the folder of current note"));
    connect(m_locateAct, &QAction::triggered,
            this, &VSearchResultTree::locateCurrentItem);

    m_addToCartAct = new QAction(VIconUtils::menuIcon(":/resources/icons/cart.svg"),
                                 tr("Add To Cart"),
                                 this);
    m_addToCartAct->setToolTip(tr("Add selected notes to Cart for further processing"));
    connect(m_addToCartAct, &QAction::triggered,
            this, &VSearchResultTree::addSelectedItemsToCart);

    m_pinToHistoryAct = new QAction(VIconUtils::menuIcon(":/resources/icons/pin.svg"),
                                    tr("Pin To History"),
                                    this);
    m_pinToHistoryAct->setToolTip(tr("Pin selected notes to History"));
    connect(m_pinToHistoryAct, &QAction::triggered,
            this, &VSearchResultTree::pinSelectedItemsToHistory);
}

void VSearchResultTree::updateResults(const QList<QSharedPointer<VSearchResultItem> > &p_items)
{
    clearResults();

    for (auto const & it : p_items) {
        appendItem(it);
    }

    emit countChanged(topLevelItemCount());
}

void VSearchResultTree::addResultItem(const QSharedPointer<VSearchResultItem> &p_item)
{
    appendItem(p_item);

    emit countChanged(topLevelItemCount());
}

void VSearchResultTree::addResultItems(const QList<QSharedPointer<VSearchResultItem> > &p_items)
{
    for (auto const & it : p_items) {
        appendItem(it);
    }

    emit countChanged(topLevelItemCount());
}

void VSearchResultTree::clearResults()
{
    clearAll();

    m_data.clear();

    emit countChanged(topLevelItemCount());
}

void VSearchResultTree::appendItem(const QSharedPointer<VSearchResultItem> &p_item)
{
    m_data.append(p_item);

    QTreeWidgetItem *item = new QTreeWidgetItem(this);
    item->setData(0, Qt::UserRole, m_data.size() - 1);
    item->setText(0, p_item->m_text.isEmpty() ? p_item->m_path : p_item->m_text);
    item->setToolTip(0, p_item->m_path);

    switch (p_item->m_type) {
    case VSearchResultItem::Note:
        item->setIcon(0, m_noteIcon);
        break;

    case VSearchResultItem::Folder:
        item->setIcon(0, m_folderIcon);
        break;

    case VSearchResultItem::Notebook:
        item->setIcon(0, m_notebookIcon);
        break;

    default:
        break;
    }

    for (auto const & it: p_item->m_matches) {
        QTreeWidgetItem *subItem = new QTreeWidgetItem(item);
        QString text;
        if (it.m_lineNumber > -1) {
            text = QString("[%1] %2").arg(it.m_lineNumber).arg(it.m_text);
        } else {
            text = it.m_text;
        }

        subItem->setText(0, text);
        subItem->setToolTip(0, it.m_text);
    }
}

void VSearchResultTree::handleContextMenuRequested(QPoint p_pos)
{
    QTreeWidgetItem *item = itemAt(p_pos);
    if (!item) {
        return;
    }

    QMenu menu(this);
    menu.setToolTipsVisible(true);

    QList<QTreeWidgetItem *> items = selectedItems();

    bool hasNote = false;
    for (auto const & it : items) {
        if (itemResultType(it) == VSearchResultItem::Note) {
            hasNote = true;
            break;
        }
    }

    if (items.size() == 1) {
        menu.addAction(m_openAct);

        if (hasNote) {
            menu.addAction(m_locateAct);
        }
    }

    if (hasNote) {
        menu.addAction(m_addToCartAct);
        menu.addAction(m_pinToHistoryAct);
    }

    menu.exec(mapToGlobal(p_pos));
}

void VSearchResultTree::locateCurrentItem()
{
    auto item =  currentItem();
    if (!item) {
        return;
    }

    const QSharedPointer<VSearchResultItem> &resItem = itemResultData(item);
    if (resItem->m_type == VSearchResultItem::Note) {
        VFile *file = g_vnote->getInternalFile(resItem->m_path);
        if (file) {
            g_mainWin->locateFile(file);
        }
    }
}

void VSearchResultTree::addSelectedItemsToCart()
{
    QList<QTreeWidgetItem *> items = selectedItems();
    VCart *cart = g_mainWin->getCart();

    int nrAdded = 0;
    for (int i = 0; i < items.size(); ++i) {
        const QSharedPointer<VSearchResultItem> &resItem = itemResultData(items[i]);
        if (resItem->m_type == VSearchResultItem::Note) {
            cart->addFile(resItem->m_path);
            ++nrAdded;
        }
    }

    if (nrAdded) {
        g_mainWin->showStatusMessage(tr("%1 %2 added to Cart")
                                       .arg(nrAdded)
                                       .arg(nrAdded > 1 ? tr("notes") : tr("note")));
    }
}

void VSearchResultTree::pinSelectedItemsToHistory()
{
    QList<QTreeWidgetItem *> items = selectedItems();
    QStringList files;
    for (int i = 0; i < items.size(); ++i) {
        const QSharedPointer<VSearchResultItem> &resItem = itemResultData(items[i]);
        if (resItem->m_type == VSearchResultItem::Note) {
            files << resItem->m_path;
        }
    }

    if (!files.isEmpty()) {
        g_mainWin->getHistoryList()->pinFiles(files);
        g_mainWin->showStatusMessage(tr("%1 %2 pinned to History")
                                       .arg(files.size())
                                       .arg(files.size() > 1 ? tr("notes") : tr("note")));
    }
}

VSearchResultItem::ItemType VSearchResultTree::itemResultType(const QTreeWidgetItem *p_item) const
{
    Q_ASSERT(p_item);
    return itemResultData(p_item)->m_type;
}

const QSharedPointer<VSearchResultItem> &VSearchResultTree::itemResultData(const QTreeWidgetItem *p_item) const
{
    Q_ASSERT(p_item);
    const QTreeWidgetItem *topItem = VUtils::topLevelTreeItem(p_item);
    int idx = topItem->data(0, Qt::UserRole).toInt();
    Q_ASSERT(idx >= 0 && idx < m_data.size());
    return m_data[idx];
}

void VSearchResultTree::activateItem(const QTreeWidgetItem *p_item) const
{
    if (!p_item) {
        return;
    }

    const QSharedPointer<VSearchResultItem> &resItem = itemResultData(p_item);
    switch (resItem->m_type) {
    case VSearchResultItem::Note:
    {
        QStringList files(resItem->m_path);
        g_mainWin->openFiles(files);
        break;
    }

    case VSearchResultItem::Folder:
    {
        VDirectory *dir = g_vnote->getInternalDirectory(resItem->m_path);
        if (dir) {
            g_mainWin->locateDirectory(dir);
        }

        break;
    }

    case VSearchResultItem::Notebook:
    {
        VNotebook *nb = g_vnote->getNotebook(resItem->m_path);
        if (nb) {
            g_mainWin->locateNotebook(nb);
        }

        break;
    }

    default:
        break;
    }
}
