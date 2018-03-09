#include "vsearchresulttree.h"

#include "utils/viconutils.h"
#include "vnote.h"
#include "vmainwindow.h"
#include "vnotebookselector.h"

extern VNote *g_vnote;

extern VMainWindow *g_mainWin;

VSearchResultTree::VSearchResultTree(QWidget *p_parent)
    : VTreeWidget(p_parent)
{
    setColumnCount(1);
    setHeaderHidden(true);
    setExpandsOnDoubleClick(false);

    setSimpleSearchMatchFlags(getSimpleSearchMatchFlags() & ~Qt::MatchRecursive);

    m_noteIcon = VIconUtils::treeViewIcon(":/resources/icons/note_item.svg");
    m_folderIcon = VIconUtils::treeViewIcon(":/resources/icons/dir_item.svg");
    m_notebookIcon = VIconUtils::treeViewIcon(":/resources/icons/notebook_item.svg");

    connect(this, &VTreeWidget::itemActivated,
            this, &VSearchResultTree::handleItemActivated);
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

void VSearchResultTree::handleItemActivated(QTreeWidgetItem *p_item, int p_column)
{
    Q_UNUSED(p_column);
    if (!p_item) {
        return;
    }

    QTreeWidgetItem *topItem = p_item;
    if (p_item->parent()) {
        topItem = p_item->parent();
    }

    int idx = topItem->data(0, Qt::UserRole).toInt();
    Q_ASSERT(idx >= 0 && idx < m_data.size());

    const QSharedPointer<VSearchResultItem> &resItem = m_data[idx];
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
            g_mainWin->getNotebookSelector()->locateNotebook(nb);
        }

        break;
    }

    default:
        break;
    }
}
