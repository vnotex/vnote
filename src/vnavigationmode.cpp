#include "vnavigationmode.h"

#include <QDebug>
#include <QLabel>
#include <QListWidget>
#include <QTreeWidget>
#include <QScrollBar>

#include "vnote.h"
#include "utils/vutils.h"

extern VNote *g_vnote;

VNavigationMode::VNavigationMode()
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
    for (int i = 0; i < p_widget->count(); ++i) {
        QListWidgetItem *item = p_widget->item(i);
        if (!item->isHidden() && item->flags() != Qt::NoItemFlags) {
            items.append(item);
        }
    }

    return items;
}

static QList<QTreeWidgetItem *> getVisibleChildItems(const QTreeWidgetItem *p_item)
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

QList<QTreeWidgetItem *> VNavigationMode::getVisibleItems(const QTreeWidget *p_widget) const
{
    QList<QTreeWidgetItem *> items;
    for (int i = 0; i < p_widget->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = p_widget->topLevelItem(i);
        if (!item->isHidden()) {
            items.append(item);
            if (item->isExpanded()) {
                items.append(getVisibleChildItems(item));
            }
        }
    }

    return items;
}

bool VNavigationMode::handleKeyNavigation(QListWidget *p_widget,
                                          bool &p_secondKey,
                                          int p_key,
                                          bool &p_succeed)
{
    bool ret = false;
    p_succeed = false;
    QChar keyChar = VUtils::keyToChar(p_key);
    if (p_secondKey && !keyChar.isNull()) {
        p_secondKey = false;
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
            p_secondKey = true;
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
    m_keyMap.clear();
    for (auto label : m_naviLabels) {
        delete label;
    }

    m_naviLabels.clear();
}

bool VNavigationMode::handleKeyNavigation(QTreeWidget *p_widget,
                                          bool &p_secondKey,
                                          int p_key,
                                          bool &p_succeed)
{
    bool ret = false;
    p_succeed = false;
    QChar keyChar = VUtils::keyToChar(p_key);
    if (p_secondKey && !keyChar.isNull()) {
        p_secondKey = false;
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
            p_secondKey = true;
        }

        ret = true;
    }

    return ret;
}
