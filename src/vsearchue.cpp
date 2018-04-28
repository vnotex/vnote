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
#include "vdirectory.h"
#include "vdirectorytree.h"
#include "veditarea.h"

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
        return tr("List and search all the notebooks");

    case ID::Name_FolderNote_AllNotebook:
        return tr("Search the name of folders/notes in all the notebooks");

    case ID::Content_Note_AllNotebook:
        return tr("Search the content of notes in all the notebooks");

    case ID::Name_FolderNote_CurrentNotebook:
        return tr("Search the name of folders/notes in current notebook");

    case ID::Content_Note_CurrentNotebook:
        return tr("Search the content of notes in current notebook");

    case ID::Name_FolderNote_CurrentFolder:
        return tr("Search the name of folders/notes in current folder");

    case ID::Content_Note_CurrentFolder:
        return tr("Search the content of notes in current folder");

    case ID::Name_Note_Buffer:
        return tr("List and search the name of opened notes in buffer");

    case ID::Content_Note_Buffer:
        return tr("Search the content of opened notes in buffer");

    case ID::Outline_Note_Buffer:
        return tr("Search the outline of opened notes in buffer");

    case ID::Path_FolderNote_AllNotebook:
        return tr("Search the path of folders/notes in all the notebooks");

    case ID::Path_FolderNote_CurrentNotebook:
        return tr("Search the path of folders/notes in current notebook");

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
    case ID::Name_FolderNote_CurrentNotebook:
    case ID::Name_FolderNote_CurrentFolder:
    case ID::Name_Note_Buffer:
    case ID::Path_FolderNote_AllNotebook:
    case ID::Path_FolderNote_CurrentNotebook:
        return m_listWidget;

    case ID::Content_Note_AllNotebook:
    case ID::Content_Note_CurrentNotebook:
    case ID::Content_Note_CurrentFolder:
    case ID::Content_Note_Buffer:
    case ID::Outline_Note_Buffer:
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

    case ID::Name_FolderNote_CurrentNotebook:
        searchNameOfFolderNoteInCurrentNotebook(p_cmd);
        break;

    case ID::Content_Note_CurrentNotebook:
        searchContentOfNoteInCurrentNotebook(p_cmd);
        break;

    case ID::Name_FolderNote_CurrentFolder:
        searchNameOfFolderNoteInCurrentFolder(p_cmd);
        break;

    case ID::Content_Note_CurrentFolder:
        searchContentOfNoteInCurrentFolder(p_cmd);
        break;

    case ID::Name_Note_Buffer:
        searchNameOfBuffer(p_cmd);
        break;

    case ID::Content_Note_Buffer:
        searchContentOfBuffer(p_cmd);
        break;

    case ID::Outline_Note_Buffer:
        searchOutlineOfBuffer(p_cmd);
        break;

    case ID::Path_FolderNote_AllNotebook:
        searchPathOfFolderNoteInAllNotebooks(p_cmd);
        break;

    case ID::Path_FolderNote_CurrentNotebook:
        searchPathOfFolderNoteInCurrentNotebook(p_cmd);
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
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::AllNotebooks,
                                                               VSearchConfig::Name,
                                                               VSearchConfig::Notebook,
                                                               VSearchConfig::Internal,
                                                               VSearchConfig::Fuzzy,
                                                               p_cmd,
                                                               QString()));
        m_search->setConfig(config);

        QSharedPointer<VSearchResult> result = m_search->search(notebooks);
        handleSearchFinished(result);
    }
}

void VSearchUE::searchNameOfFolderNoteInAllNotebooks(const QString &p_cmd)
{
    if (p_cmd.isEmpty()) {
        m_inSearch = false;
        emit stateUpdated(State::Success);
    } else {
        m_search->clear();
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::AllNotebooks,
                                                               VSearchConfig::Name,
                                                               VSearchConfig::Folder | VSearchConfig::Note,
                                                               VSearchConfig::Internal,
                                                               VSearchConfig::NoneOption,
                                                               p_cmd,
                                                               QString()));
        m_search->setConfig(config);
        QSharedPointer<VSearchResult> result = m_search->search(g_vnote->getNotebooks());
        handleSearchFinished(result);
    }
}

void VSearchUE::searchContentOfNoteInAllNotebooks(const QString &p_cmd)
{
    if (p_cmd.isEmpty()) {
        m_inSearch = false;
        emit stateUpdated(State::Success);
    } else {
        m_search->clear();
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::AllNotebooks,
                                                               VSearchConfig::Content,
                                                               VSearchConfig::Note,
                                                               VSearchConfig::Internal,
                                                               VSearchConfig::NoneOption,
                                                               p_cmd,
                                                               QString()));
        m_search->setConfig(config);
        QSharedPointer<VSearchResult> result = m_search->search(g_vnote->getNotebooks());
        handleSearchFinished(result);
    }
}

void VSearchUE::searchNameOfFolderNoteInCurrentNotebook(const QString &p_cmd)
{
    if (p_cmd.isEmpty()) {
        m_inSearch = false;
        emit stateUpdated(State::Success);
    } else {
        QVector<VNotebook *> notebooks;
        notebooks.append(g_mainWin->getNotebookSelector()->currentNotebook());
        m_search->clear();
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::CurrentNotebook,
                                                               VSearchConfig::Name,
                                                               VSearchConfig::Folder | VSearchConfig::Note,
                                                               VSearchConfig::Internal,
                                                               VSearchConfig::NoneOption,
                                                               p_cmd,
                                                               QString()));
        m_search->setConfig(config);
        QSharedPointer<VSearchResult> result = m_search->search(notebooks);
        handleSearchFinished(result);
    }
}

void VSearchUE::searchContentOfNoteInCurrentNotebook(const QString &p_cmd)
{
    if (p_cmd.isEmpty()) {
        m_inSearch = false;
        emit stateUpdated(State::Success);
    } else {
        QVector<VNotebook *> notebooks;
        notebooks.append(g_mainWin->getNotebookSelector()->currentNotebook());
        m_search->clear();
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::CurrentNotebook,
                                                               VSearchConfig::Content,
                                                               VSearchConfig::Note,
                                                               VSearchConfig::Internal,
                                                               VSearchConfig::NoneOption,
                                                               p_cmd,
                                                               QString()));
        m_search->setConfig(config);
        QSharedPointer<VSearchResult> result = m_search->search(notebooks);
        handleSearchFinished(result);
    }
}

void VSearchUE::searchNameOfFolderNoteInCurrentFolder(const QString &p_cmd)
{
    if (p_cmd.isEmpty()) {
        m_inSearch = false;
        emit stateUpdated(State::Success);
    } else {
        VDirectory *dir = g_mainWin->getDirectoryTree()->currentDirectory();
        m_search->clear();
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::CurrentFolder,
                                                               VSearchConfig::Name,
                                                               VSearchConfig::Folder | VSearchConfig::Note,
                                                               VSearchConfig::Internal,
                                                               VSearchConfig::NoneOption,
                                                               p_cmd,
                                                               QString()));
        m_search->setConfig(config);
        QSharedPointer<VSearchResult> result = m_search->search(dir);
        handleSearchFinished(result);
    }
}

void VSearchUE::searchContentOfNoteInCurrentFolder(const QString &p_cmd)
{
    if (p_cmd.isEmpty()) {
        m_inSearch = false;
        emit stateUpdated(State::Success);
    } else {
        VDirectory *dir = g_mainWin->getDirectoryTree()->currentDirectory();
        m_search->clear();
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::CurrentFolder,
                                                               VSearchConfig::Content,
                                                               VSearchConfig::Note,
                                                               VSearchConfig::Internal,
                                                               VSearchConfig::NoneOption,
                                                               p_cmd,
                                                               QString()));
        m_search->setConfig(config);
        QSharedPointer<VSearchResult> result = m_search->search(dir);
        handleSearchFinished(result);
    }
}

QVector<VFile *> getFilesInBuffer()
{
    QVector<VEditTabInfo> tabs = g_mainWin->getEditArea()->getAllTabsInfo();
    QVector<VFile *> files;
    files.reserve(tabs.size());
    for (auto const & ta : tabs) {
        files.append(ta.m_editTab->getFile());
    }

    return files;
}

void VSearchUE::searchNameOfBuffer(const QString &p_cmd)
{
    QVector<VFile *> files = getFilesInBuffer();
    if (p_cmd.isEmpty()) {
        // List all the notes.
        for (auto const & fi : files) {
            QSharedPointer<VSearchResultItem> item(new VSearchResultItem(VSearchResultItem::Note,
                                                                         VSearchResultItem::LineNumber,
                                                                         fi->getName(),
                                                                         fi->fetchPath()));
            handleSearchItemAdded(item);
        }

        m_inSearch = false;
        emit stateUpdated(State::Success);
    } else {
        m_search->clear();
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::OpenedNotes,
                                                               VSearchConfig::Name,
                                                               VSearchConfig::Note,
                                                               VSearchConfig::Internal,
                                                               VSearchConfig::NoneOption,
                                                               p_cmd,
                                                               QString()));
        m_search->setConfig(config);
        QSharedPointer<VSearchResult> result = m_search->search(files);
        handleSearchFinished(result);
    }
}

void VSearchUE::searchContentOfBuffer(const QString &p_cmd)
{
    if (p_cmd.isEmpty()) {
        m_inSearch = false;
        emit stateUpdated(State::Success);
    } else {
        m_search->clear();
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::OpenedNotes,
                                                               VSearchConfig::Content,
                                                               VSearchConfig::Note,
                                                               VSearchConfig::Internal,
                                                               VSearchConfig::NoneOption,
                                                               p_cmd,
                                                               QString()));
        m_search->setConfig(config);
        QSharedPointer<VSearchResult> result = m_search->search(getFilesInBuffer());
        handleSearchFinished(result);
    }
}

void VSearchUE::searchOutlineOfBuffer(const QString &p_cmd)
{
    if (p_cmd.isEmpty()) {
        m_inSearch = false;
        emit stateUpdated(State::Success);
    } else {
        m_search->clear();
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::OpenedNotes,
                                                               VSearchConfig::Outline,
                                                               VSearchConfig::Note,
                                                               VSearchConfig::Internal,
                                                               VSearchConfig::NoneOption,
                                                               p_cmd,
                                                               QString()));
        m_search->setConfig(config);
        QSharedPointer<VSearchResult> result = m_search->search(getFilesInBuffer());
        handleSearchFinished(result);
    }
}

void VSearchUE::searchPathOfFolderNoteInAllNotebooks(const QString &p_cmd)
{
    if (p_cmd.isEmpty()) {
        m_inSearch = false;
        emit stateUpdated(State::Success);
    } else {
        m_search->clear();
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::AllNotebooks,
                                                               VSearchConfig::Path,
                                                               VSearchConfig::Folder | VSearchConfig::Note,
                                                               VSearchConfig::Internal,
                                                               VSearchConfig::NoneOption,
                                                               p_cmd,
                                                               QString()));
        m_search->setConfig(config);
        QSharedPointer<VSearchResult> result = m_search->search(g_vnote->getNotebooks());
        handleSearchFinished(result);
    }
}

void VSearchUE::searchPathOfFolderNoteInCurrentNotebook(const QString &p_cmd)
{
    if (p_cmd.isEmpty()) {
        m_inSearch = false;
        emit stateUpdated(State::Success);
    } else {
        QVector<VNotebook *> notebooks;
        notebooks.append(g_mainWin->getNotebookSelector()->currentNotebook());
        m_search->clear();
        QSharedPointer<VSearchConfig> config(new VSearchConfig(VSearchConfig::CurrentNotebook,
                                                               VSearchConfig::Path,
                                                               VSearchConfig::Folder | VSearchConfig::Note,
                                                               VSearchConfig::Internal,
                                                               VSearchConfig::NoneOption,
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

void VSearchUE::handleSearchItemAdded(const QSharedPointer<VSearchResultItem> &p_item)
{
    static int itemAdded = 0;
    ++itemAdded;

    QCoreApplication::sendPostedEvents(NULL, QEvent::KeyPress);

    switch (m_id) {
    case ID::Name_Notebook_AllNotebook:
    case ID::Name_FolderNote_AllNotebook:
    case ID::Name_FolderNote_CurrentNotebook:
    case ID::Name_FolderNote_CurrentFolder:
    case ID::Name_Note_Buffer:
    case ID::Path_FolderNote_AllNotebook:
    case ID::Path_FolderNote_CurrentNotebook:
        appendItemToList(p_item);
        if (itemAdded > 50) {
            itemAdded = 0;
            m_listWidget->updateGeometry();
            emit widgetUpdated();
        }

        break;

    case ID::Content_Note_AllNotebook:
    case ID::Content_Note_CurrentNotebook:
    case ID::Content_Note_CurrentFolder:
    case ID::Content_Note_Buffer:
    case ID::Outline_Note_Buffer:
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
    case ID::Name_FolderNote_CurrentNotebook:
    case ID::Name_FolderNote_CurrentFolder:
    case ID::Name_Note_Buffer:
    case ID::Path_FolderNote_AllNotebook:
    case ID::Path_FolderNote_CurrentNotebook:
    {
        for (auto const & it : p_items) {
            appendItemToList(it);
        }

        m_listWidget->updateGeometry();
        emit widgetUpdated();
        break;
    }

    case ID::Content_Note_AllNotebook:
    case ID::Content_Note_CurrentNotebook:
    case ID::Content_Note_CurrentFolder:
    case ID::Content_Note_Buffer:
    case ID::Outline_Note_Buffer:
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
    // We put notebook and folder before note.
    int row = 0;
    switch (p_item->m_type) {
    case VSearchResultItem::Note:
        row = m_listWidget->count();
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

    QListWidgetItem *item = m_listWidget->insertDoubleRowsItem(row, *icon, first, second);
    item->setData(Qt::UserRole, m_data.size() - 1);
    item->setToolTip(p_item->m_path);

    if (row == 0) {
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
            g_mainWin->locateNotebook(nb);
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
    case ID::Name_FolderNote_CurrentNotebook:
    case ID::Name_FolderNote_CurrentFolder:
    case ID::Name_Note_Buffer:
    case ID::Path_FolderNote_AllNotebook:
    case ID::Path_FolderNote_CurrentNotebook:
    {
        // Could not use postEvent method here which will induce infinite recursion.
        m_listWidget->selectNextItem(p_forward);
        break;
    }

    case ID::Content_Note_AllNotebook:
    case ID::Content_Note_CurrentNotebook:
    case ID::Content_Note_CurrentFolder:
    case ID::Content_Note_Buffer:
    case ID::Outline_Note_Buffer:
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
    case ID::Name_FolderNote_CurrentNotebook:
    case ID::Name_FolderNote_CurrentFolder:
    case ID::Name_Note_Buffer:
    case ID::Path_FolderNote_AllNotebook:
    case ID::Path_FolderNote_CurrentNotebook:
    {
        activateItem(m_listWidget->currentItem());
        break;
    }

    case ID::Content_Note_AllNotebook:
    case ID::Content_Note_CurrentNotebook:
    case ID::Content_Note_CurrentFolder:
    case ID::Content_Note_Buffer:
    case ID::Outline_Note_Buffer:
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

void VSearchUE::selectParentItem(int p_id)
{
    switch (p_id) {
    case ID::Content_Note_AllNotebook:
    case ID::Content_Note_CurrentNotebook:
    case ID::Content_Note_CurrentFolder:
    case ID::Content_Note_Buffer:
    case ID::Outline_Note_Buffer:
        m_treeWidget->selectParentItem();
        break;

    default:
        break;
    }
}

void VSearchUE::toggleItemExpanded(int p_id)
{
    switch (p_id) {
    case ID::Content_Note_AllNotebook:
    case ID::Content_Note_CurrentNotebook:
    case ID::Content_Note_CurrentFolder:
    case ID::Content_Note_Buffer:
    case ID::Outline_Note_Buffer:
    {
        QTreeWidgetItem *item = m_treeWidget->currentItem();
        if (item) {
            item->setExpanded(!item->isExpanded());
        }

        break;
    }

    default:
        break;
    }
}

void VSearchUE::sort(int p_id)
{
    static bool noteFirst = false;

    switch (p_id) {
    case ID::Name_Notebook_AllNotebook:
    case ID::Name_FolderNote_AllNotebook:
    case ID::Name_FolderNote_CurrentNotebook:
    case ID::Name_FolderNote_CurrentFolder:
    case ID::Name_Note_Buffer:
    case ID::Path_FolderNote_AllNotebook:
    case ID::Path_FolderNote_CurrentNotebook:
    {
        int cnt = m_listWidget->count();
        if (noteFirst) {
            int idx = cnt - 1;
            while (true) {
                if (itemResultData(m_listWidget->item(idx))->m_type != VSearchResultItem::Note) {
                    // Move it to the first row.
                    m_listWidget->moveItem(idx, 0);
                } else {
                    break;
                }
            }
        } else {
            int idx = 0;
            while (true) {
                if (itemResultData(m_listWidget->item(idx))->m_type != VSearchResultItem::Note) {
                    // Move it to the last row.
                    m_listWidget->moveItem(idx, cnt - 1);
                } else {
                    break;
                }
            }
        }

        if (cnt) {
            m_listWidget->setCurrentRow(0);
        }

        noteFirst = !noteFirst;
        break;
    }

    default:
        break;
    }
}

QString VSearchUE::currentItemFolder(int p_id)
{
    QString folder;
    QSharedPointer<VSearchResultItem> resItem;

    switch (p_id) {
    case ID::Name_Notebook_AllNotebook:
    case ID::Name_FolderNote_AllNotebook:
    case ID::Name_FolderNote_CurrentNotebook:
    case ID::Name_FolderNote_CurrentFolder:
    case ID::Name_Note_Buffer:
    case ID::Path_FolderNote_AllNotebook:
    case ID::Path_FolderNote_CurrentNotebook:
    {
        QListWidgetItem *item = m_listWidget->currentItem();
        if (item) {
            resItem = itemResultData(item);
        }

        break;
    }

    case ID::Content_Note_AllNotebook:
    case ID::Content_Note_CurrentNotebook:
    case ID::Content_Note_CurrentFolder:
    case ID::Content_Note_Buffer:
    case ID::Outline_Note_Buffer:
    {
        QTreeWidgetItem *item = m_treeWidget->currentItem();
        if (item) {
            resItem = itemResultData(item);
        }

        break;
    }

    default:
        Q_ASSERT(false);
    }

    if (!resItem.isNull()) {
        switch (resItem->m_type) {
        case VSearchResultItem::Note:
            folder = VUtils::basePathFromPath(resItem->m_path);
            break;

        case VSearchResultItem::Folder:
        case VSearchResultItem::Notebook:
            folder = resItem->m_path;
            break;

        default:
            break;
        }
    }

    return folder;
}
