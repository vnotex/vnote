#include "voutlineue.h"

#include <QTreeWidgetItem>
#include <QListWidgetItem>

#include "vtreewidget.h"
#include "vlistwidget.h"
#include "voutline.h"
#include "vmainwindow.h"
#include "vedittab.h"
#include "veditarea.h"
#include "vsearchconfig.h"
#include "vtableofcontent.h"

extern VMainWindow *g_mainWin;

VOutlineUE::VOutlineUE(QObject *p_parent)
    : IUniversalEntry(p_parent),
      m_listWidget(NULL),
      m_treeWidget(NULL),
      m_listOutline(true)
{
}

QString VOutlineUE::description(int p_id) const
{
    Q_UNUSED(p_id);

    return tr("List and search the outline of current note");
}

void VOutlineUE::init()
{
    if (m_initialized) {
        return;
    }

    Q_ASSERT(m_widgetParent);

    m_initialized = true;

    m_listWidget = new VListWidget(m_widgetParent);
    m_listWidget->setFitContent(true);
    m_listWidget->hide();
    connect(m_listWidget, SIGNAL(itemActivated(QListWidgetItem *)),
            this, SLOT(activateItem(QListWidgetItem *)));

    m_treeWidget = new VTreeWidget(m_widgetParent);
    m_treeWidget->setColumnCount(1);
    m_treeWidget->setHeaderHidden(true);
    m_treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    m_treeWidget->setExpandsOnDoubleClick(false);
    m_treeWidget->setFitContent(true);
    m_treeWidget->hide();

    connect(m_treeWidget, SIGNAL(itemActivated(QTreeWidgetItem *, int)),
            this, SLOT(activateItem(QTreeWidgetItem *, int)));
    connect(m_treeWidget, &VTreeWidget::itemExpanded,
            this, &VOutlineUE::widgetUpdated);
}

QWidget *VOutlineUE::widget(int p_id)
{
    Q_UNUSED(p_id);
    init();

    if (m_listOutline) {
        return m_treeWidget;
    } else {
        return m_listWidget;
    }
}

void VOutlineUE::processCommand(int p_id, const QString &p_cmd)
{
    Q_UNUSED(p_id);

    init();

    clear(-1);

    emit stateUpdated(State::Busy);

    VEditTab *tab = g_mainWin->getCurrentTab();
    if (p_cmd.isEmpty()) {
        // List the outline.
        m_listOutline = true;

        if (tab) {
            const VTableOfContent &outline = tab->getOutline();
            VOutline::updateTreeFromOutline(m_treeWidget, outline);
            m_treeWidget->expandAll();

            const VHeaderPointer &header = tab->getCurrentHeader();
            if (outline.isMatched(header)) {
                VOutline::selectHeader(m_treeWidget, outline, header);
            }
        }
    } else {
        // Search the outline.
        m_listOutline = false;

        VSearchConfig config(VSearchConfig::CurrentNote,
                             VSearchConfig::Content,
                             VSearchConfig::Note,
                             VSearchConfig::Internal,
                             VSearchConfig::NoneOption,
                             p_cmd,
                             QString());

        if (tab) {
            const VTableOfContent &outline = tab->getOutline();
            const QVector<VTableOfContentItem> &table = outline.getTable();
            for (auto const & it : table) {
                if (it.isEmpty()) {
                    continue;
                }

                if (!config.m_token.matched(it.m_name)) {
                    continue;
                }

                // Add item to list.
                QListWidgetItem *item = new QListWidgetItem(it.m_name, m_listWidget);
                item->setData(Qt::UserRole, it.m_index);
                item->setToolTip(it.m_name);

                if (!m_listWidget->currentItem()) {
                    m_listWidget->setCurrentItem(item);
                }
            }
        }
    }

    emit stateUpdated(State::Success);
    updateWidget();
}

void VOutlineUE::updateWidget()
{
    QWidget *wid = m_listWidget;
    if (m_listOutline) {
        if (m_treeWidget->topLevelItemCount() > 0) {
            m_treeWidget->resizeColumnToContents(0);
        } else {
            QTreeWidgetItem *item = new QTreeWidgetItem(m_treeWidget, QStringList("test"));
            m_treeWidget->resizeColumnToContents(0);
            delete item;
        }

        wid = m_listWidget;
    }

    wid->updateGeometry();
    emit widgetUpdated();
}

void VOutlineUE::clear(int p_id)
{
    Q_UNUSED(p_id);
    m_treeWidget->clearAll();
    m_listWidget->clearAll();
}

void VOutlineUE::entryHidden(int p_id)
{
    Q_UNUSED(p_id);
    clear(-1);
}

void VOutlineUE::entryShown(int p_id, const QString &p_cmd)
{
    processCommand(p_id, p_cmd);
}

void VOutlineUE::selectNextItem(int p_id, bool p_forward)
{
    Q_UNUSED(p_id);

    if (m_listOutline) {
        m_treeWidget->selectNextItem(p_forward);
    } else {
        m_listWidget->selectNextItem(p_forward);
    }
}

void VOutlineUE::activate(int p_id)
{
    Q_UNUSED(p_id);
    if (m_listOutline) {
        activateItem(m_treeWidget->currentItem(), 0);
    } else {
        activateItem(m_listWidget->currentItem());
    }
}

void VOutlineUE::askToStop(int p_id)
{
    Q_UNUSED(p_id);
}

void VOutlineUE::activateItem(QListWidgetItem *p_item)
{
    if (!p_item) {
        return;
    }

    int idx = p_item->data(Qt::UserRole).toInt();

    emit requestHideUniversalEntry();

    VHeaderPointer hp(g_mainWin->getCurrentFile(), idx);
    g_mainWin->getEditArea()->scrollToHeader(hp);
}

void VOutlineUE::activateItem(QTreeWidgetItem *p_item, int p_col)
{
    Q_UNUSED(p_col);
    if (!p_item) {
        return;
    }

    VEditTab *tab = g_mainWin->getCurrentTab();
    Q_ASSERT(tab);
    const VTableOfContent &outline = tab->getOutline();
    const VTableOfContentItem *header = VOutline::getHeaderFromItem(p_item, outline);
    Q_ASSERT(header);
    if (!header->isEmpty()) {
        emit requestHideUniversalEntry();

        VHeaderPointer hp(outline.getFile(), header->m_index);
        g_mainWin->getEditArea()->scrollToHeader(hp);
    }
}
