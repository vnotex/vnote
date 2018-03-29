#include "vsearchue.h"

#include <QDebug>
#include <QVector>

#include "vlistwidgetdoublerows.h"
#include "vtreewidget.h"
#include "vnotebook.h"
#include "vnote.h"
#include "vsearch.h"
#include "utils/viconutils.h"
#include "utils/vutils.h"
#include "vmainwindow.h"
#include "vnotebookselector.h"
#include "vnotefile.h"

extern VNote *g_vnote;

extern VMainWindow *g_mainWin;

#define ITEM_NUM_TO_UPDATE_WIDGET 20

VSearchUE::VSearchUE(QObject *p_parent)
    : IUniversalEntry(p_parent),
      m_search(NULL),
      m_inSearch(false),
      m_id(ID::Name_Notebook_AllNotebook),
      m_listWidget(NULL),
      m_treeWidget(NULL)
{
}

QString VSearchUE::description(int p_id) const
{
    switch (p_id) {
    case ID::Name_Notebook_AllNotebook:
        return tr("List and search all notebooks");

    case ID::Name_FolderNote_AllNotebook:
        return tr("Search the name of folders/notes in all notebooks");

    case ID::Content_Note_AllNotebook:
        return tr("Search the content of notes in all notebooks");

    default:
        Q_ASSERT(false);
        return tr("Invalid ID %1").arg(p_id);
    }
}

void VSearchUE::init()
{
    if (m_initialized) {
        return;
    }

    Q_ASSERT(m_widgetParent);

    m_initialized = true;

    m_search = new VSearch(this);
    connect(m_search, &VSearch::resultItemAdded,
            this, &VSearchUE::handleSearchItemAdded);
    connect(m_search, &VSearch::resultItemsAdded,
            this, &VSearchUE::handleSearchItemsAdded);
    connect(m_search, &VSearch::finished,
            this, &VSearchUE::handleSearchFinished);

    m_noteIcon = VIconUtils::treeViewIcon(":/resources/icons/note_item.svg");
    m_folderIcon = VIconUtils::treeViewIcon(":/resources/icons/dir_item.svg");
    m_notebookIcon = VIconUtils::treeViewIcon(":/resources/icons/notebook_item.svg");

    m_listWidget = new VListWidgetDoubleRows(m_widgetParent);
    m_listWidget->setFitContent(true);
    m_listWidget->hide();
    connect(m_listWidget, SIGNAL(itemActivated(QListWidgetItem *)),
            this, SLOT(activateItem(QListWidgetItem *)));

    m_treeWidget = new VTreeWidget(m_widgetParent);
    m_treeWidget->setColumnCount(1);
    m_treeWidget->setHeaderHidden(true);
    m_treeWidget->setExpandsOnDoubleClick(false);
    m_treeWidget->setSimpleSearchMatchFlags(m_treeWidget->getSimpleSearchMatchFlags() & ~Qt::MatchRecursive);
    m_treeWidget->setFitContent(true);
    m_treeWidget->hide();
    connect(m_treeWidget, SIGNAL(itemActivated(QTreeWidgetItem *, int)),
            this, SLOT(activateItem(QTreeWidgetItem *, int)));
    connect(m_treeWidget, &VTreeWidget::itemExpanded,
            this, &VSearchUE::widgetUpdated);
}

QWidget *VSearchUE::widget(int p_id)
{
    init();

    switch (p_id) {
    case ID::Name_Notebook_AllNotebook:
    case ID::Name_FolderNote_AllNotebook:
        return m_listWidget;

    case ID::Content_Note_AllNotebook:
        return m_treeWidget;

    default:
        Q_ASSERT(false);
        return NULL;
    }
}

void VSearchUE::processCommand(int p_id, const QString &p_cmd)
{
    qDebug() << "processCommand" << p_id << p_cmd;

    init();

    clear(-1);

    m_inSearch = true;
    m_id = p_id;
    emit stateUpdated(State::Busy);
    switch (p_id) {
    case ID::Name_Notebook_AllNotebook:
        searchNameOfAllNotebooks(p_cmd);
        break;

    case ID::Name_FolderNote_AllNotebook:
        searchNameOfFolderNoteInAllNotebooks(p_cmd);
        break;

    case ID::Content_Note_AllNotebook:
        searchContentOfNoteInAllNotebooks(p_cmd);
        break;

    default:
        Q_ASSERT(false);
        break;
    }

    updateWidget();
}

void VSearchUE::updateWidget()
{
    QWidget *wid = widget(m_id);
    if (wid == m_treeWidget) {
        if (m_treeWidget->topLevelItemCount() > 0) {
            m_treeWidget->resizeColumnToContents(0);
        } else {
            QTreeWidgetItem *item = new QTreeWidgetItem(m_treeWidget, QStringList("test"));
            m_treeWidget->resizeColumnToContents(0);
            delete item;
        }
    }

    wid->updateGeometry();
    emit widgetUpdated();
}

void VSearchUE::searchNameOfAllNotebooks(const QString &p_cmd)
{
    const QVector<VNotebook *> &notebooks = g_vnote->getNotebooks();
    if (p_cmd.isEmpty()) {
        // List all the notebooks.
        for (auto const & nb : notebooks) {
            QSharedPointer<VSearchResultItem> item(new VSearchResultItem(VSearchResultItem::Notebook,
                                                                         VSearchResultItem::LineNumber,
                                                                         nb->getName(),
                                                                         nb->getPath()));
            handleSearchItemAdded(item);
        }

        m_inSearch = false;
        emit stateUpdated(State::Success);
    } else {
        // Do a fuzzy search against the name of the notebooks.
        m_search->clear();
        VSearchConfig::Option opt = VSearchConfig::Fuzzy;
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::AllNotebooks,
                                                               VSearchConfig::Name,
                                                               VSearchConfig::Notebook,
                                                               VSearchConfig::Internal,
                                                               opt,
                                                               p_cmd,
                                                               QString()));
        m_search->setConfig(config);

        QSharedPointer<VSearchResult> result = m_search->search(notebooks);
        handleSearchFinished(result);
    }
}

void VSearchUE::searchNameOfFolderNoteInAllNotebooks(const QString &p_cmd)
{
    const QVector<VNotebook *> &notebooks = g_vnote->getNotebooks();
    if (p_cmd.isEmpty()) {
        m_inSearch = false;
        emit stateUpdated(State::Success);
    } else {
        m_search->clear();
        VSearchConfig::Option opt = VSearchConfig::NoneOption;
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::AllNotebooks,
                                                               VSearchConfig::Name,
                                                               VSearchConfig::Folder | VSearchConfig::Note,
                                                               VSearchConfig::Internal,
                                                               opt,
                                                               p_cmd,
                                                               QString()));
        m_search->setConfig(config);
        QSharedPointer<VSearchResult> result = m_search->search(notebooks);
        handleSearchFinished(result);
    }
}

void VSearchUE::searchContentOfNoteInAllNotebooks(const QString &p_cmd)
{
    const QVector<VNotebook *> &notebooks = g_vnote->getNotebooks();
    if (p_cmd.isEmpty()) {
        m_inSearch = false;
        emit stateUpdated(State::Success);
    } else {
        m_search->clear();
        VSearchConfig::Option opt = VSearchConfig::NoneOption;
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::AllNotebooks,
                                                               VSearchConfig::Content,
                                                               VSearchConfig::Note,
                                                               VSearchConfig::Internal,
                                                               opt,
                                                               p_cmd,
                                                               QString()));
        m_search->setConfig(config);
        QSharedPointer<VSearchResult> result = m_search->search(notebooks);
        handleSearchFinished(result);
    }
}

void VSearchUE::clear(int p_id)
{
    Q_UNUSED(p_id);
    stopSearch();

    m_data.clear();
    m_listWidget->clearAll();
    m_treeWidget->clearAll();
}

void VSearchUE::entryHidden(int p_id)
{
    Q_UNUSED(p_id);
}

void VSearchUE::handleSearchItemAdded(const QSharedPointer<VSearchResultItem> &p_item)
{
    static int itemAdded = 0;
    ++itemAdded;

    QCoreApplication::sendPostedEvents(NULL, QEvent::KeyPress);

    switch (m_id) {
    case ID::Name_Notebook_AllNotebook:
    case ID::Name_FolderNote_AllNotebook:
        appendItemToList(p_item);
        if (itemAdded > 50) {
            itemAdded = 0;
            m_listWidget->updateGeometry();
            emit widgetUpdated();
        }

        break;

    case ID::Content_Note_AllNotebook:
        appendItemToTree(p_item);
        if (itemAdded > 50) {
            itemAdded = 0;
            m_treeWidget->resizeColumnToContents(0);
            m_treeWidget->updateGeometry();
            emit widgetUpdated();
        }

        break;

    default:
        break;
    }
}

void VSearchUE::handleSearchItemsAdded(const QList<QSharedPointer<VSearchResultItem> > &p_items)
{
    QCoreApplication::sendPostedEvents(NULL, QEvent::KeyPress);

    switch (m_id) {
    case ID::Name_Notebook_AllNotebook:
    case ID::Name_FolderNote_AllNotebook:
    {
        for (auto const & it : p_items) {
            appendItemToList(it);
        }

        m_listWidget->updateGeometry();
        emit widgetUpdated();
        break;
    }

    case ID::Content_Note_AllNotebook:
    {
        for (auto const & it : p_items) {
            appendItemToTree(it);
        }

        m_treeWidget->resizeColumnToContents(0);
        m_treeWidget->updateGeometry();
        emit widgetUpdated();
        break;
    }

    default:
        break;
    }
}

void VSearchUE::appendItemToList(const QSharedPointer<VSearchResultItem> &p_item)
{
    m_data.append(p_item);

    QString first, second;
    if (p_item->m_text.isEmpty()) {
        first = p_item->m_path;
    } else {
        first = p_item->m_text;
        second = p_item->m_path;
    }

    QIcon *icon = NULL;
    switch (p_item->m_type) {
    case VSearchResultItem::Note:
        icon = &m_noteIcon;
        break;

    case VSearchResultItem::Folder:
        icon = &m_folderIcon;
        break;

    case VSearchResultItem::Notebook:
        icon = &m_notebookIcon;
        break;

    default:
        break;
    }

    QListWidgetItem *item = m_listWidget->addItem(*icon, first, second);
    item->setData(Qt::UserRole, m_data.size() - 1);
    item->setToolTip(p_item->m_path);

    if (m_listWidget->currentRow() == -1) {
        m_listWidget->setCurrentRow(0);
    }
}

void VSearchUE::appendItemToTree(const QSharedPointer<VSearchResultItem> &p_item)
{
    m_data.append(p_item);

    QTreeWidgetItem *item = new QTreeWidgetItem(m_treeWidget);
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

    if (!m_treeWidget->currentItem()) {
        m_treeWidget->setCurrentItem(item);
    }
}

void VSearchUE::handleSearchFinished(const QSharedPointer<VSearchResult> &p_result)
{
    Q_ASSERT(m_inSearch);
    Q_ASSERT(p_result->m_state != VSearchState::Idle);

    qDebug() << "handleSearchFinished" << (int)p_result->m_state;

    IUniversalEntry::State state = State::Idle;

    bool finished = true;
    switch (p_result->m_state) {
    case VSearchState::Busy:
        qDebug() << "search is ongoing";
        state = State::Busy;
        finished = false;
        break;

    case VSearchState::Success:
        qDebug() << "search succeeded";
        state = State::Success;
        break;

    case VSearchState::Fail:
        qDebug() << "search failed";
        state = State::Fail;
        break;

    case VSearchState::Cancelled:
        qDebug() << "search cancelled";
        state = State::Cancelled;
        break;

    default:
        break;
    }

    if (finished) {
        m_search->clear();
        m_inSearch = false;
    }

    updateWidget();

    emit stateUpdated(state);
}

void VSearchUE::stopSearch()
{
    if (m_inSearch) {
        m_search->stop();

        while (m_inSearch) {
            VUtils::sleepWait(100);
            qDebug() << "sleep wait for search to stop";
        }
    }
}

const QSharedPointer<VSearchResultItem> &VSearchUE::itemResultData(const QListWidgetItem *p_item) const
{
    Q_ASSERT(p_item);
    int idx = p_item->data(Qt::UserRole).toInt();
    Q_ASSERT(idx >= 0 && idx < m_data.size());
    return m_data[idx];
}

const QSharedPointer<VSearchResultItem> &VSearchUE::itemResultData(const QTreeWidgetItem *p_item) const
{
    Q_ASSERT(p_item);
    const QTreeWidgetItem *topItem = VUtils::topLevelTreeItem(p_item);
    int idx = topItem->data(0, Qt::UserRole).toInt();
    Q_ASSERT(idx >= 0 && idx < m_data.size());
    return m_data[idx];
}

void VSearchUE::activateItem(const QSharedPointer<VSearchResultItem> &p_item)
{
    switch (p_item->m_type) {
    case VSearchResultItem::Note:
    {
        QStringList files(p_item->m_path);
        g_mainWin->openFiles(files);
        break;
    }

    case VSearchResultItem::Folder:
    {
        VDirectory *dir = g_vnote->getInternalDirectory(p_item->m_path);
        if (dir) {
            g_mainWin->locateDirectory(dir);
        }

        break;
    }

    case VSearchResultItem::Notebook:
    {
        VNotebook *nb = g_vnote->getNotebook(p_item->m_path);
        if (nb) {
            g_mainWin->getNotebookSelector()->locateNotebook(nb);
        }

        break;
    }

    default:
        break;
    }
}

void VSearchUE::activateItem(QListWidgetItem *p_item)
{
    if (!p_item) {
        return;
    }

    emit requestHideUniversalEntry();
    activateItem(itemResultData(p_item));
}

void VSearchUE::activateItem(QTreeWidgetItem *p_item, int p_col)
{
    Q_UNUSED(p_col);
    if (!p_item) {
        return;
    }

    emit requestHideUniversalEntry();
    activateItem(itemResultData(p_item));
}

void VSearchUE::selectNextItem(int p_id, bool p_forward)
{
    switch (p_id) {
    case ID::Name_Notebook_AllNotebook:
    case ID::Name_FolderNote_AllNotebook:
    {
        // Could not use postEvent method here which will induce infinite recursion.
        m_listWidget->selectNextItem(p_forward);
        break;
    }

    case ID::Content_Note_AllNotebook:
    {
        m_treeWidget->selectNextItem(p_forward);
        break;
    }

    default:
        Q_ASSERT(false);
    }
}

void VSearchUE::activate(int p_id)
{
    switch (p_id) {
    case ID::Name_Notebook_AllNotebook:
    case ID::Name_FolderNote_AllNotebook:
    {
        activateItem(m_listWidget->currentItem());
        break;
    }

    case ID::Content_Note_AllNotebook:
    {
        activateItem(m_treeWidget->currentItem(), 0);
        break;
    }

    default:
        Q_ASSERT(false);
    }
}

void VSearchUE::askToStop(int p_id)
{
    Q_UNUSED(p_id);
    if (m_inSearch) {
        m_search->stop();
    }
}
