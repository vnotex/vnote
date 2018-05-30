#include "vlistfolderue.h"

#include <QListWidgetItem>
#include <QLabel>
#include <QVBoxLayout>

#include "vlistwidget.h"
#include "vdirectory.h"
#include "vdirectorytree.h"
#include "vmainwindow.h"
#include "vnote.h"
#include "utils/viconutils.h"
#include "vnotefile.h"
#include "vsearchue.h"
#include "utils/vutils.h"
#include "vnotebook.h"
#include "vuetitlecontentpanel.h"

extern VMainWindow *g_mainWin;

extern VNote *g_vnote;

VListFolderUE::VListFolderUE(QObject *p_parent)
    : IUniversalEntry(p_parent),
      m_listWidget(NULL)
{
}

QString VListFolderUE::description(int p_id) const
{
    Q_UNUSED(p_id);

    return tr("List and search the folders and notes of current folder");
}

void VListFolderUE::init()
{
    if (m_initialized) {
        return;
    }

    m_initialized = true;

    m_noteIcon = VIconUtils::treeViewIcon(":/resources/icons/note_item.svg");
    m_folderIcon = VIconUtils::treeViewIcon(":/resources/icons/dir_item.svg");

    m_listWidget = new VListWidget(m_widgetParent);
    m_listWidget->setFitContent(true);
    connect(m_listWidget, SIGNAL(itemActivated(QListWidgetItem *)),
            this, SLOT(activateItem(QListWidgetItem *)));

    m_panel = new VUETitleContentPanel(m_listWidget, m_widgetParent);
    m_panel->hide();
}

QWidget *VListFolderUE::widget(int p_id)
{
    Q_UNUSED(p_id);

    init();

    return m_panel;
}

void VListFolderUE::processCommand(int p_id, const QString &p_cmd)
{
    Q_UNUSED(p_id);

    init();

    QString folderPath = m_folderPath;
    if (folderPath.isEmpty()) {
        VDirectory *dir = g_mainWin->getDirectoryTree()->currentDirectory();
        folderPath = dir->fetchPath();
    }

    listFolder(folderPath, p_cmd);

    m_listWidget->updateGeometry();
    emit widgetUpdated();
    emit stateUpdated(State::Success);
}

void VListFolderUE::clear(int p_id)
{
    Q_UNUSED(p_id);

    m_panel->clearTitle();
    m_listWidget->clearAll();
    m_data.clear();

    m_folderPath.clear();
    m_currentFolderPath.clear();
}

void VListFolderUE::selectNextItem(int p_id, bool p_forward)
{
    Q_UNUSED(p_id);

    m_listWidget->selectNextItem(p_forward);
}

void VListFolderUE::selectParentItem(int p_id)
{
    Q_UNUSED(p_id);

    if (m_currentFolderPath.isEmpty()) {
        return;
    }

    if (listFolder(VUtils::basePathFromPath(m_currentFolderPath), QString())) {
        m_folderPath = m_currentFolderPath;
    }

    m_listWidget->updateGeometry();
    emit widgetUpdated();
}

void VListFolderUE::activate(int p_id)
{
    Q_UNUSED(p_id);
    activateItem(m_listWidget->currentItem());
}

const QSharedPointer<VSearchResultItem> &VListFolderUE::itemResultData(const QListWidgetItem *p_item) const
{
    Q_ASSERT(p_item);
    int idx = p_item->data(Qt::UserRole).toInt();
    Q_ASSERT(idx >= 0 && idx < m_data.size());
    return m_data[idx];
}

void VListFolderUE::activateItem(QListWidgetItem *p_item)
{
    if (!p_item) {
        return;
    }

    emit requestHideUniversalEntry();

    VSearchUE::activateItem(itemResultData(p_item));
}

void VListFolderUE::sort(int p_id)
{
    Q_UNUSED(p_id);
    static bool noteFirst = false;

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
}

void VListFolderUE::addResultItem(const QSharedPointer<VSearchResultItem> &p_item)
{
    m_data.append(p_item);

    QString text;
    if (p_item->m_text.isEmpty()) {
        text = p_item->m_path;
    } else {
        text = p_item->m_text;
    }

    QIcon *icon = NULL;
    switch (p_item->m_type) {
    case VSearchResultItem::Note:
        icon = &m_noteIcon;
        break;

    case VSearchResultItem::Folder:
        icon = &m_folderIcon;
        break;

    default:
        break;
    }

    QListWidgetItem *item = new QListWidgetItem(*icon, text);
    item->setData(Qt::UserRole, m_data.size() - 1);
    item->setToolTip(p_item->m_path);

    m_listWidget->addItem(item);

    if (m_listWidget->count() == 1) {
        m_listWidget->setCurrentRow(0);
    }
}

void VListFolderUE::entryShown(int p_id, const QString &p_cmd)
{
    processCommand(p_id, p_cmd);
}

void VListFolderUE::setFolderPath(const QString &p_path)
{
    m_folderPath = p_path;
}

QString VListFolderUE::currentItemFolder(int p_id)
{
    Q_UNUSED(p_id);
    QString folder;
    QListWidgetItem *item = m_listWidget->currentItem();
    if (item) {
        const QSharedPointer<VSearchResultItem> &resItem = itemResultData(item);
        if (resItem->m_type == VSearchResultItem::Folder) {
            folder = resItem->m_path;
        }
    }

    return folder;
}

bool VListFolderUE::listFolder(const QString &p_path, const QString &p_cmd)
{
    VDirectory *dir = g_vnote->getInternalDirectory(p_path);
    if (!dir) {
        // See if it is a notebook.
        VNotebook *nb = g_vnote->getNotebook(p_path);
        if (!nb) {
            return false;
        }

        dir = nb->getRootDir();
    }

    m_panel->clearTitle();
    m_listWidget->clearAll();
    m_data.clear();

    m_currentFolderPath = dir->fetchPath();
    m_panel->setTitle(m_currentFolderPath);

    if (!dir->open()) {
        return true;
    }

    if (p_cmd.isEmpty()) {
        // List the content.
        for (auto const & it : dir->getSubDirs()) {
            QSharedPointer<VSearchResultItem> item(new VSearchResultItem(VSearchResultItem::Folder,
                                                                         VSearchResultItem::LineNumber,
                                                                         it->getName(),
                                                                         it->fetchPath()));
            addResultItem(item);
        }

        for (auto const & file : dir->getFiles()) {
            QSharedPointer<VSearchResultItem> item(new VSearchResultItem(VSearchResultItem::Note,
                                                                         VSearchResultItem::LineNumber,
                                                                         file->getName(),
                                                                         file->fetchPath()));
            addResultItem(item);
        }
    } else {
        // Search the content.
        VSearchConfig config(VSearchConfig::CurrentFolder,
                             VSearchConfig::Name,
                             VSearchConfig::Note | VSearchConfig::Folder,
                             VSearchConfig::Internal,
                             VSearchConfig::NoneOption,
                             p_cmd,
                             QString());

        for (auto const & it : dir->getSubDirs()) {
            QString name = it->getName();
            if (!config.m_token.matched(name)) {
                continue;
            }

            QSharedPointer<VSearchResultItem> item(new VSearchResultItem(VSearchResultItem::Folder,
                                                                         VSearchResultItem::LineNumber,
                                                                         name,
                                                                         it->fetchPath()));
            addResultItem(item);
        }

        for (auto const & file : dir->getFiles()) {
            QString name = file->getName();
            if (!config.m_token.matched(name)) {
                continue;
            }

            QSharedPointer<VSearchResultItem> item(new VSearchResultItem(VSearchResultItem::Note,
                                                                         VSearchResultItem::LineNumber,
                                                                         name,
                                                                         file->fetchPath()));
            addResultItem(item);
        }
    }

    return true;
}
