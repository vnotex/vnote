#include "vsearchue.h"

#include <QDebug>
#include <QVector>

#include "vlistwidgetdoublerows.h"
#include "vnotebook.h"
#include "vnote.h"
#include "vsearch.h"
#include "utils/viconutils.h"
#include "vmainwindow.h"
#include "vnotebookselector.h"
#include "vnotefile.h"

extern VNote *g_vnote;

extern VMainWindow *g_mainWin;

VSearchUE::VSearchUE(QObject *p_parent)
    : IUniversalEntry(p_parent),
      m_search(NULL),
      m_inSearch(false),
      m_listWidget(NULL),
      m_id(ID::Name_Notebook_AllNotebook)
{
}

QString VSearchUE::description(int p_id) const
{
    switch (p_id) {
    case ID::Name_Notebook_AllNotebook:
        return tr("List and search all notebooks");

    case ID::Name_FolderNote_AllNotebook:
        return tr("Search the name of folders/notes in all notebooks");

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
    connect(m_search, &VSearch::finished,
            this, &VSearchUE::handleSearchFinished);

    m_noteIcon = VIconUtils::treeViewIcon(":/resources/icons/note_item.svg");
    m_folderIcon = VIconUtils::treeViewIcon(":/resources/icons/dir_item.svg");
    m_notebookIcon = VIconUtils::treeViewIcon(":/resources/icons/notebook_item.svg");

    m_listWidget = new VListWidgetDoubleRows(m_widgetParent);
    m_listWidget->setFitContent(true);
    m_listWidget->hide();
    connect(m_listWidget, &VListWidgetDoubleRows::itemActivated,
            this, &VSearchUE::activateItem);
}

QWidget *VSearchUE::widget(int p_id)
{
    init();

    switch (p_id) {
    case ID::Name_Notebook_AllNotebook:
        V_FALLTHROUGH;
    case ID::Name_FolderNote_AllNotebook:
        return m_listWidget;

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

    default:
        Q_ASSERT(false);
        break;
    }

    widget(p_id)->updateGeometry();
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

void VSearchUE::clear(int p_id)
{
    Q_UNUSED(p_id);
    stopSearch();

    m_data.clear();
    m_listWidget->clearAll();
}

void VSearchUE::entryHidden(int p_id)
{
    Q_UNUSED(p_id);
}

void VSearchUE::handleSearchItemAdded(const QSharedPointer<VSearchResultItem> &p_item)
{
    switch (m_id) {
    case ID::Name_Notebook_AllNotebook:
        V_FALLTHROUGH;
    case ID::Name_FolderNote_AllNotebook:
        appendItemToList(p_item);
        break;

    default:
        break;
    }
}

void VSearchUE::appendItemToList(const QSharedPointer<VSearchResultItem> &p_item)
{
    static int itemAdded = 0;
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

    if (++itemAdded >= 10) {
        itemAdded = 0;
        m_listWidget->updateGeometry();
        emit widgetUpdated();
    }
}

void VSearchUE::handleSearchFinished(const QSharedPointer<VSearchResult> &p_result)
{
    Q_ASSERT(m_inSearch);
    Q_ASSERT(p_result->m_state != VSearchState::Idle);

    qDebug() << "handleSearchFinished" << (int)p_result->m_state;

    IUniversalEntry::State state = State::Idle;

    switch (p_result->m_state) {
    case VSearchState::Busy:
        qDebug() << "search is ongoing";
        state = State::Busy;
        return;

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

    m_search->clear();
    m_inSearch = false;

    widget(m_id)->updateGeometry();
    emit widgetUpdated();

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

void VSearchUE::activateItem(const QListWidgetItem *p_item)
{
    if (!p_item) {
        return;
    }

    emit requestHideUniversalEntry();

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
            g_mainWin->getNotebookSelector()->locateNotebook(nb);
        }

        break;
    }

    default:
        break;
    }
}

void VSearchUE::selectNextItem(int p_id, bool p_forward)
{
    switch (p_id) {
    case ID::Name_Notebook_AllNotebook:
        V_FALLTHROUGH;
    case ID::Name_FolderNote_AllNotebook:
    {
        // Could not use postEvent method here which will induce infinite recursion.
        m_listWidget->selectNextItem(p_forward);
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
        V_FALLTHROUGH;
    case ID::Name_FolderNote_AllNotebook:
    {
        activateItem(m_listWidget->currentItem());
        break;
    }

    default:
        Q_ASSERT(false);
    }
}

void VSearchUE::askToStop(int p_id)
{
    Q_UNUSED(p_id);
    m_search->stop();
}
