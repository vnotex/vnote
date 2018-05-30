#include "vlistue.h"

#include <QListWidgetItem>
#include <QLabel>
#include <QVBoxLayout>

#include "vlistwidgetdoublerows.h"
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
#include "vhistorylist.h"

extern VMainWindow *g_mainWin;

extern VNote *g_vnote;

VListUE::VListUE(QObject *p_parent)
    : IUniversalEntry(p_parent),
      m_listWidget(NULL)
{
}

QString VListUE::description(int p_id) const
{
    switch (p_id) {
    case ID::History:
        return tr("List and search history");

    default:
        Q_ASSERT(false);
        return tr("Invalid ID %1").arg(p_id);
    }
}

void VListUE::init()
{
    if (m_initialized) {
        return;
    }

    m_initialized = true;

    m_noteIcon = VIconUtils::treeViewIcon(":/resources/icons/note_item.svg");
    m_folderIcon = VIconUtils::treeViewIcon(":/resources/icons/dir_item.svg");

    m_listWidget = new VListWidgetDoubleRows(m_widgetParent);
    m_listWidget->setFitContent(true);
    connect(m_listWidget, SIGNAL(itemActivated(QListWidgetItem *)),
            this, SLOT(activateItem(QListWidgetItem *)));

    m_panel = new VUETitleContentPanel(m_listWidget, m_widgetParent);
    m_panel->hide();
}

QWidget *VListUE::widget(int p_id)
{
    Q_UNUSED(p_id);

    init();

    return m_panel;
}

void VListUE::processCommand(int p_id, const QString &p_cmd)
{
    Q_UNUSED(p_id);

    init();

    clear(-1);

    switch (p_id) {
    case ID::History:
        listHistory(p_cmd);
        break;

    default:
        break;
    }

    m_listWidget->updateGeometry();
    emit widgetUpdated();
    emit stateUpdated(State::Success);
}

void VListUE::clear(int p_id)
{
    Q_UNUSED(p_id);

    m_panel->clearTitle();
    m_listWidget->clearAll();
    m_data.clear();
}

void VListUE::selectNextItem(int p_id, bool p_forward)
{
    Q_UNUSED(p_id);

    m_listWidget->selectNextItem(p_forward);
}

void VListUE::activate(int p_id)
{
    Q_UNUSED(p_id);
    activateItem(m_listWidget->currentItem());
}

const QSharedPointer<VSearchResultItem> &VListUE::itemResultData(const QListWidgetItem *p_item) const
{
    Q_ASSERT(p_item);
    int idx = p_item->data(Qt::UserRole).toInt();
    Q_ASSERT(idx >= 0 && idx < m_data.size());
    return m_data[idx];
}

void VListUE::activateItem(QListWidgetItem *p_item)
{
    if (!p_item) {
        return;
    }

    emit requestHideUniversalEntry();

    VSearchUE::activateItem(itemResultData(p_item));
}

void VListUE::sort(int p_id)
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

void VListUE::addResultItem(const QSharedPointer<VSearchResultItem> &p_item)
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

    default:
        break;
    }

    QListWidgetItem *item = m_listWidget->addDoubleRowsItem(*icon, first, second);
    item->setData(Qt::UserRole, m_data.size() - 1);
    item->setToolTip(p_item->m_path);

    if (m_listWidget->count() == 1) {
        m_listWidget->setCurrentRow(0);
    }
}

void VListUE::listHistory(const QString &p_cmd)
{
    m_panel->setTitle(tr("History"));

    VHistoryList *history = g_mainWin->getHistoryList();
    const QLinkedList<VHistoryEntry> &entries = history->getHistoryEntries();
    if (p_cmd.isEmpty()) {
        // List the content.
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            QSharedPointer<VSearchResultItem> item(new VSearchResultItem(it->m_isFolder ? VSearchResultItem::Folder : VSearchResultItem::Note,
                                                                         VSearchResultItem::LineNumber,
                                                                         VUtils::fileNameFromPath(it->m_file),
                                                                         it->m_file));
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

        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            QString name = VUtils::fileNameFromPath(it->m_file);
            if (!config.m_token.matched(name)) {
                continue;
            }

            QSharedPointer<VSearchResultItem> item(new VSearchResultItem(it->m_isFolder ? VSearchResultItem::Folder : VSearchResultItem::Note,
                                                                         VSearchResultItem::LineNumber,
                                                                         name,
                                                                         it->m_file));
            addResultItem(item);
        }
    }
}
