#include <QDebug>
#include <QVector>
#include <QString>
#include <QKeyEvent>
#include <QLabel>
#include <QCoreApplication>
#include "voutline.h"
#include "vtoc.h"
#include "utils/vutils.h"
#include "vnote.h"
#include "vconfigmanager.h"

extern VNote *g_vnote;
extern VConfigManager vconfig;

VOutline::VOutline(QWidget *parent)
    : QTreeWidget(parent), VNavigationMode()
{
    setColumnCount(1);
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::SingleSelection);

    connect(this, &VOutline::currentItemChanged,
            this, &VOutline::handleCurItemChanged);
}

void VOutline::checkOutline(const VToc &p_toc) const
{
    const QVector<VHeader> &headers = p_toc.headers;

    for (int i = 0; i < headers.size(); ++i) {
        V_ASSERT(headers[i].index == i);
    }
}

void VOutline::updateOutline(const VToc &toc)
{
    // Clear current header
    curHeader = VAnchor();

    checkOutline(toc);

    outline = toc;

    updateTreeFromOutline();

    expandTree();
}

void VOutline::updateTreeFromOutline()
{
    clear();

    if (!outline.valid) {
        return;
    }

    const QVector<VHeader> &headers = outline.headers;
    int idx = 0;
    updateTreeByLevel(headers, idx, NULL, NULL, 1);
}

void VOutline::updateTreeByLevel(const QVector<VHeader> &headers, int &index,
                                 QTreeWidgetItem *parent, QTreeWidgetItem *last, int level)
{
    while (index < headers.size()) {
        const VHeader &header = headers[index];
        QTreeWidgetItem *item;
        if (header.level == level) {
            if (parent) {
                item = new QTreeWidgetItem(parent);
            } else {
                item = new QTreeWidgetItem(this);
            }

            fillItem(item, header);

            last = item;
            ++index;
        } else if (header.level < level) {
            return;
        } else {
            updateTreeByLevel(headers, index, last, NULL, level + 1);
        }
    }
}

void VOutline::fillItem(QTreeWidgetItem *p_item, const VHeader &p_header)
{
    p_item->setData(0, Qt::UserRole, p_header.index);
    p_item->setText(0, p_header.name);
    p_item->setToolTip(0, p_header.name);

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

void VOutline::handleCurItemChanged(QTreeWidgetItem *p_curItem, QTreeWidgetItem * /*p_preItem*/)
{
    if (!p_curItem) {
        return;
    }

    const VHeader *header = getHeaderFromItem(p_curItem);
    if (!header) {
        return;
    }

    VAnchor tmp(outline.m_file, header->anchor, header->lineNumber, header->index);
    if (tmp == curHeader) {
        return;
    }

    curHeader = tmp;

    if (!header->isEmpty()) {
        emit outlineItemActivated(curHeader);
    }
}

void VOutline::updateCurHeader(const VAnchor &anchor)
{
    if (anchor == curHeader) {
        return;
    }

    curHeader = anchor;
    if (outline.type == VHeaderType::Anchor) {
        selectAnchor(anchor.anchor);
    } else {
        // Select by lineNumber.
        selectLineNumber(anchor.lineNumber);
    }
}

void VOutline::selectAnchor(const QString &anchor)
{
    setCurrentItem(NULL);

    if (anchor.isEmpty()) {
        return;
    }

    int nrTop = topLevelItemCount();
    for (int i = 0; i < nrTop; ++i) {
        if (selectAnchorOne(topLevelItem(i), anchor)) {
            return;
        }
    }
}

bool VOutline::selectAnchorOne(QTreeWidgetItem *item, const QString &anchor)
{
    if (!item) {
        return false;
    }

    const VHeader *header = getHeaderFromItem(item);
    if (!header) {
        return false;
    }

    if (header->anchor == anchor) {
        setCurrentItem(item);
        return true;
    }

    int nrChild = item->childCount();
    for (int i = 0; i < nrChild; ++i) {
        if (selectAnchorOne(item->child(i), anchor)) {
            return true;
        }
    }
    return false;
}

void VOutline::selectLineNumber(int lineNumber)
{
    setCurrentItem(NULL);

    if (lineNumber == -1) {
        return;
    }

    int nrTop = topLevelItemCount();
    for (int i = 0; i < nrTop; ++i) {
        if (selectLineNumberOne(topLevelItem(i), lineNumber)) {
            return;
        }
    }
}

bool VOutline::selectLineNumberOne(QTreeWidgetItem *item, int lineNumber)
{
    if (!item) {
        return false;
    }

    const VHeader *header = getHeaderFromItem(item);
    if (!header) {
        return false;
    }

    if (header->lineNumber == lineNumber) {
        // Select this item
        setCurrentItem(item);
        return true;
    }

    int nrChild = item->childCount();
    for (int i = 0; i < nrChild; ++i) {
        if (selectLineNumberOne(item->child(i), lineNumber)) {
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

const VHeader *VOutline::getHeaderFromItem(QTreeWidgetItem *p_item) const
{
    const VHeader *header = NULL;

    int index = p_item->data(0, Qt::UserRole).toInt();
    if (index < 0 || index >= outline.headers.size()) {
        return header;
    }

    header = &(outline.headers[index]);
    Q_ASSERT(header->index == index);

    return header;
}
