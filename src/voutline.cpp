#include <QVector>
#include <QString>
#include <QKeyEvent>
#include <QLabel>
#include <QCoreApplication>
#include "voutline.h"
#include "utils/vutils.h"
#include "vnote.h"
#include "vfile.h"

extern VNote *g_vnote;

VOutline::VOutline(QWidget *parent)
    : VTreeWidget(parent),
      VNavigationMode(),
      m_muted(false)
{
    setColumnCount(1);
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::SingleSelection);

    // TODO: jump to the header when user click the same item twice.
    connect(this, &VOutline::currentItemChanged,
            this, &VOutline::handleCurrentItemChanged);
}

void VOutline::updateOutline(const VTableOfContent &p_outline)
{
    if (p_outline == m_outline) {
        return;
    }

    // Clear current header
    m_currentHeader.clear();

    m_outline = p_outline;

    updateTreeFromOutline(this, m_outline);

    expandTree();
}

void VOutline::updateTreeFromOutline(QTreeWidget *p_treeWidget,
                                     const VTableOfContent &p_outline)
{
    p_treeWidget->clear();

    if (p_outline.isEmpty()) {
        return;
    }

    const QVector<VTableOfContentItem> &headers = p_outline.getTable();
    int idx = 0;
    updateTreeByLevel(p_treeWidget, headers, idx, NULL, NULL, 1);
}

void VOutline::updateTreeByLevel(QTreeWidget *p_treeWidget,
                                 const QVector<VTableOfContentItem> &p_headers,
                                 int &p_index,
                                 QTreeWidgetItem *p_parent,
                                 QTreeWidgetItem *p_last,
                                 int p_level)
{
    while (p_index < p_headers.size()) {
        const VTableOfContentItem &header = p_headers[p_index];
        QTreeWidgetItem *item;
        if (header.m_level == p_level) {
            if (p_parent) {
                item = new QTreeWidgetItem(p_parent);
            } else {
                item = new QTreeWidgetItem(p_treeWidget);
            }

            fillItem(item, header);

            p_last = item;
            ++p_index;
        } else if (header.m_level < p_level) {
            return;
        } else {
            updateTreeByLevel(p_treeWidget, p_headers, p_index, p_last, NULL, p_level + 1);
        }
    }
}

void VOutline::fillItem(QTreeWidgetItem *p_item, const VTableOfContentItem &p_header)
{
    p_item->setData(0, Qt::UserRole, p_header.m_index);
    p_item->setText(0, p_header.m_name);
    p_item->setToolTip(0, p_header.m_name);

    if (p_header.isEmpty()) {
        p_item->setForeground(0, QColor("grey"));
    }
}

void VOutline::expandTree()
{
    if (topLevelItemCount() == 0) {
        return;
    }

    expandAll();
}

void VOutline::handleCurrentItemChanged(QTreeWidgetItem *p_curItem,
                                        QTreeWidgetItem * p_preItem)
{
    Q_UNUSED(p_preItem);

    if (!p_curItem) {
        return;
    }

    const VTableOfContentItem *header = getHeaderFromItem(p_curItem, m_outline);
    Q_ASSERT(header);
    m_currentHeader.update(m_outline.getFile(), header->m_index);

    if (!header->isEmpty() && !m_muted) {
        emit outlineItemActivated(m_currentHeader);
    }
}

void VOutline::updateCurrentHeader(const VHeaderPointer &p_header)
{
    if (p_header == m_currentHeader
        || !m_outline.isMatched(p_header)) {
        return;
    }

    // Item change should not emit the signal.
    m_muted = true;
    m_currentHeader = p_header;
    selectHeader(this, m_outline, m_currentHeader);
    m_muted = false;
}

void VOutline::selectHeader(QTreeWidget *p_treeWidget,
                            const VTableOfContent &p_outline,
                            const VHeaderPointer &p_header)
{
    p_treeWidget->setCurrentItem(NULL);

    if (!p_outline.getItem(p_header)) {
        return;
    }

    int nrTop = p_treeWidget->topLevelItemCount();
    for (int i = 0; i < nrTop; ++i) {
        if (selectHeaderOne(p_treeWidget, p_treeWidget->topLevelItem(i), p_outline, p_header)) {
            return;
        }
    }
}

bool VOutline::selectHeaderOne(QTreeWidget *p_treeWidget,
                               QTreeWidgetItem *p_item,
                               const VTableOfContent &p_outline,
                               const VHeaderPointer &p_header)
{
    if (!p_item) {
        return false;
    }

    const VTableOfContentItem *header = getHeaderFromItem(p_item, p_outline);
    if (!header) {
        return false;
    }

    if (header->isMatched(p_header)) {
        p_treeWidget->setCurrentItem(p_item);
        return true;
    }

    int nrChild = p_item->childCount();
    for (int i = 0; i < nrChild; ++i) {
        if (selectHeaderOne(p_treeWidget, p_item->child(i), p_outline, p_header)) {
            return true;
        }
    }

    return false;
}

void VOutline::keyPressEvent(QKeyEvent *event)
{
    int key = event->key();
    int modifiers = event->modifiers();

    switch (key) {
    case Qt::Key_Return:
    {
        QTreeWidgetItem *item = currentItem();
        if (item) {
            item->setExpanded(!item->isExpanded());
        }
        break;
    }

    case Qt::Key_J:
    {
        if (modifiers == Qt::ControlModifier) {
            event->accept();
            QKeyEvent *downEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Down,
                                                 Qt::NoModifier);
            QCoreApplication::postEvent(this, downEvent);
            return;
        }
        break;
    }

    case Qt::Key_K:
    {
        if (modifiers == Qt::ControlModifier) {
            event->accept();
            QKeyEvent *upEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Up,
                                               Qt::NoModifier);
            QCoreApplication::postEvent(this, upEvent);
            return;
        }
        break;
    }

    default:
        break;
    }

    VTreeWidget::keyPressEvent(event);
}

void VOutline::showNavigation()
{
    VNavigationMode::showNavigation(this);
}

bool VOutline::handleKeyNavigation(int p_key, bool &p_succeed)
{
    static bool secondKey = false;
    return VNavigationMode::handleKeyNavigation(this,
                                                secondKey,
                                                p_key,
                                                p_succeed);
}

const VTableOfContentItem *VOutline::getHeaderFromItem(QTreeWidgetItem *p_item,
                                                       const VTableOfContent &p_outline)
{
    int index = p_item->data(0, Qt::UserRole).toInt();
    return p_outline.getItem(index);
}
