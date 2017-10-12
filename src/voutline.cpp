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
    : QTreeWidget(parent),
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

    updateTreeFromOutline();

    expandTree();
}

void VOutline::updateTreeFromOutline()
{
    clear();

    if (m_outline.isEmpty()) {
        return;
    }

    const QVector<VTableOfContentItem> &headers = m_outline.getTable();
    int idx = 0;
    updateTreeByLevel(headers, idx, NULL, NULL, 1);
}

void VOutline::updateTreeByLevel(const QVector<VTableOfContentItem> &headers,
                                 int &index,
                                 QTreeWidgetItem *parent,
                                 QTreeWidgetItem *last,
                                 int level)
{
    while (index < headers.size()) {
        const VTableOfContentItem &header = headers[index];
        QTreeWidgetItem *item;
        if (header.m_level == level) {
            if (parent) {
                item = new QTreeWidgetItem(parent);
            } else {
                item = new QTreeWidgetItem(this);
            }

            fillItem(item, header);

            last = item;
            ++index;
        } else if (header.m_level < level) {
            return;
        } else {
            updateTreeByLevel(headers, index, last, NULL, level + 1);
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

    const VTableOfContentItem *header = getHeaderFromItem(p_curItem);
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
    selectHeader(m_currentHeader);
    m_muted = false;
}

void VOutline::selectHeader(const VHeaderPointer &p_header)
{
    setCurrentItem(NULL);

    if (!m_outline.getItem(p_header)) {
        return;
    }

    int nrTop = topLevelItemCount();
    for (int i = 0; i < nrTop; ++i) {
        if (selectHeaderOne(topLevelItem(i), p_header)) {
            return;
        }
    }
}

bool VOutline::selectHeaderOne(QTreeWidgetItem *p_item, const VHeaderPointer &p_header)
{
    if (!p_item) {
        return false;
    }

    const VTableOfContentItem *header = getHeaderFromItem(p_item);
    if (!header) {
        return false;
    }

    if (header->isMatched(p_header)) {
        setCurrentItem(p_item);
        return true;
    }

    int nrChild = p_item->childCount();
    for (int i = 0; i < nrChild; ++i) {
        if (selectHeaderOne(p_item->child(i), p_header)) {
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

    QTreeWidget::keyPressEvent(event);
}

void VOutline::registerNavigation(QChar p_majorKey)
{
    m_majorKey = p_majorKey;
    V_ASSERT(m_keyMap.empty());
    V_ASSERT(m_naviLabels.empty());
}

void VOutline::showNavigation()
{
    // Clean up.
    m_keyMap.clear();
    for (auto label : m_naviLabels) {
        delete label;
    }
    m_naviLabels.clear();

    if (!isVisible()) {
        return;
    }

    // Generate labels for visible items.
    auto items = getVisibleItems();
    for (int i = 0; i < 26 && i < items.size(); ++i) {
        QChar key('a' + i);
        m_keyMap[key] = items[i];

        QString str = QString(m_majorKey) + key;
        QLabel *label = new QLabel(str, this);
        label->setStyleSheet(g_vnote->getNavigationLabelStyle(str));
        label->move(visualItemRect(items[i]).topLeft());
        label->show();
        m_naviLabels.append(label);
    }
}

void VOutline::hideNavigation()
{
    m_keyMap.clear();
    for (auto label : m_naviLabels) {
        delete label;
    }
    m_naviLabels.clear();
}

bool VOutline::handleKeyNavigation(int p_key, bool &p_succeed)
{
    static bool secondKey = false;
    bool ret = false;
    p_succeed = false;
    QChar keyChar = VUtils::keyToChar(p_key);
    if (secondKey && !keyChar.isNull()) {
        secondKey = false;
        p_succeed = true;
        ret = true;
        auto it = m_keyMap.find(keyChar);
        if (it != m_keyMap.end()) {
            setCurrentItem(it.value());
            setFocus();
        }
    } else if (keyChar == m_majorKey) {
        // Major key pressed.
        // Need second key if m_keyMap is not empty.
        if (m_keyMap.isEmpty()) {
            p_succeed = true;
        } else {
            secondKey = true;
        }
        ret = true;
    }
    return ret;
}

QList<QTreeWidgetItem *> VOutline::getVisibleItems() const
{
    QList<QTreeWidgetItem *> items;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = topLevelItem(i);
        if (!item->isHidden()) {
            items.append(item);
            if (item->isExpanded()) {
                items.append(getVisibleChildItems(item));
            }
        }
    }
    return items;
}

QList<QTreeWidgetItem *> VOutline::getVisibleChildItems(const QTreeWidgetItem *p_item) const
{
    QList<QTreeWidgetItem *> items;
    if (p_item && !p_item->isHidden() && p_item->isExpanded()) {
        for (int i = 0; i < p_item->childCount(); ++i) {
            QTreeWidgetItem *child = p_item->child(i);
            if (!child->isHidden()) {
                items.append(child);
                if (child->isExpanded()) {
                    items.append(getVisibleChildItems(child));
                }
            }
        }
    }
    return items;
}

const VTableOfContentItem *VOutline::getHeaderFromItem(QTreeWidgetItem *p_item) const
{
    int index = p_item->data(0, Qt::UserRole).toInt();
    return m_outline.getItem(index);
}
