#include "vnavigationmode.h"

#include <QDebug>
#include <QLabel>
#include <QListWidget>
#include <QTreeWidget>
#include <QScrollBar>

#include "vnote.h"
#include "utils/vutils.h"
#include "vtreewidget.h"

extern VNote *g_vnote;

VNavigationMode::VNavigationMode()
    : m_isSecondKey(false)
{
}

VNavigationMode::~VNavigationMode()
{
}

void VNavigationMode::registerNavigation(QChar p_majorKey)
{
    m_majorKey = p_majorKey;
    Q_ASSERT(m_keyMap.empty());
    Q_ASSERT(m_naviLabels.empty());
}

void VNavigationMode::hideNavigation()
{
    clearNavigation();
}

void VNavigationMode::showNavigation(QListWidget *p_widget)
{
    clearNavigation();

    if (!p_widget->isVisible()) {
        return;
    }

    // Generate labels for visible items.
    auto items = getVisibleItems(p_widget);
    for (int i = 0; i < 26 && i < items.size(); ++i) {
        QChar key('a' + i);
        m_keyMap[key] = items[i];

        QString str = QString(m_majorKey) + key;
        QLabel *label = new QLabel(str, p_widget);
        label->setStyleSheet(g_vnote->getNavigationLabelStyle(str));
        label->show();
        QRect rect = p_widget->visualItemRect(items[i]);
        // Display the label at the end to show the file name.
        // Fix: take the vertical scrollbar into account.
        int extraWidth = label->width() + 2;
        QScrollBar *vbar = p_widget->verticalScrollBar();
        if (vbar && vbar->minimum() != vbar->maximum()) {
            extraWidth += vbar->width();
        }

        label->move(rect.x() + p_widget->rect().width() - extraWidth,
                    rect.y());

        m_naviLabels.append(label);
    }
}

QList<QListWidgetItem *> VNavigationMode::getVisibleItems(const QListWidget *p_widget) const
{
    QList<QListWidgetItem *> items;
    // The first visible item.
    QListWidgetItem *firstItem = p_widget->itemAt(0, 0);
    if (!firstItem) {
        return items;
    }

    QListWidgetItem *lastItem = NULL;
    lastItem = p_widget->itemAt(p_widget->viewport()->rect().bottomLeft());

    int first = p_widget->row(firstItem);
    int last = lastItem ? p_widget->row(lastItem) : (p_widget->count() - 1);

    for (int i = first; i <= last; ++i) {
        QListWidgetItem *item = p_widget->item(i);
        if (!item->isHidden() && item->flags() != Qt::NoItemFlags) {
            items.append(item);
        }
    }

    return items;
}

QList<QTreeWidgetItem *> VNavigationMode::getVisibleItems(const QTreeWidget *p_widget) const
{
    QList<QTreeWidgetItem *> items;

    // The first visible item.
    QTreeWidgetItem *firstItem = p_widget->itemAt(0, 0);
    if (!firstItem) {
        return items;
    }

    QTreeWidgetItem *lastItem = NULL;
    lastItem = p_widget->itemAt(p_widget->viewport()->rect().bottomLeft());

    QTreeWidgetItem *item = firstItem;
    while (item) {
        items.append(item);
        if (item == lastItem) {
            break;
        }

        item = VTreeWidget::nextItem(p_widget, item, true);
    }

    return items;
}

bool VNavigationMode::handleKeyNavigation(QListWidget *p_widget,
                                          int p_key,
                                          bool &p_succeed)
{
    bool ret = false;
    p_succeed = false;
    QChar keyChar = VUtils::keyToChar(p_key);
    if (m_isSecondKey && !keyChar.isNull()) {
        m_isSecondKey = false;
        p_succeed = true;
        auto it = m_keyMap.find(keyChar);
        if (it != m_keyMap.end()) {
            ret = true;
            p_widget->setCurrentItem(static_cast<QListWidgetItem *>(it.value()),
                                     QItemSelectionModel::ClearAndSelect);
            p_widget->setFocus();
        }
    } else if (keyChar == m_majorKey) {
        // Major key pressed.
        // Need second key if m_keyMap is not empty.
        if (m_keyMap.isEmpty()) {
            p_succeed = true;
        } else {
            m_isSecondKey = true;
        }

        ret = true;
    }

    return ret;
}

void VNavigationMode::showNavigation(QTreeWidget *p_widget)
{
    clearNavigation();

    if (!p_widget->isVisible()) {
        return;
    }

    // Generate labels for visible items.
    auto items = getVisibleItems(p_widget);
    for (int i = 0; i < 26 && i < items.size(); ++i) {
        QChar key('a' + i);
        m_keyMap[key] = items[i];

        QString str = QString(m_majorKey) + key;
        QLabel *label = new QLabel(str, p_widget);
        label->setStyleSheet(g_vnote->getNavigationLabelStyle(str));
        label->move(p_widget->visualItemRect(items[i]).topLeft());
        label->show();
        m_naviLabels.append(label);
    }
}

void VNavigationMode::clearNavigation()
{
    m_isSecondKey = false;

    m_keyMap.clear();
    for (auto label : m_naviLabels) {
        delete label;
    }

    m_naviLabels.clear();
}

bool VNavigationMode::handleKeyNavigation(QTreeWidget *p_widget,
                                          int p_key,
                                          bool &p_succeed)
{
    bool ret = false;
    p_succeed = false;
    QChar keyChar = VUtils::keyToChar(p_key);
    if (m_isSecondKey && !keyChar.isNull()) {
        m_isSecondKey = false;
        p_succeed = true;
        auto it = m_keyMap.find(keyChar);
        if (it != m_keyMap.end()) {
            ret = true;
            p_widget->setCurrentItem(static_cast<QTreeWidgetItem *>(it.value()));
            p_widget->setFocus();
        }
    } else if (keyChar == m_majorKey) {
        // Major key pressed.
        // Need second key if m_keyMap is not empty.
        if (m_keyMap.isEmpty()) {
            p_succeed = true;
        } else {
            m_isSecondKey = true;
        }

        ret = true;
    }

    return ret;
}
