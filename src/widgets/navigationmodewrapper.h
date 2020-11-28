#ifndef NAVIGATIONMODEWRAPPER_H
#define NAVIGATIONMODEWRAPPER_H

#include "navigationmode.h"

#include <QLabel>
#include <QScrollBar>

#include <QListWidgetItem>
#include <QTreeWidgetItem>

#include "listwidget.h"
#include "treewidget.h"

namespace vnotex
{
    template <typename T, typename I>
    class NavigationModeWrapper : public NavigationMode
    {
    public:
        NavigationModeWrapper(T *p_widget);

    // NavigationMode.
    protected:
        QVector<void *> getVisibleNavigationItems() Q_DECL_OVERRIDE;

        void placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label) Q_DECL_OVERRIDE;

        void handleTargetHit(void *p_item) Q_DECL_OVERRIDE;

    private:
        QVector<I *> getVisibleItems() const
        {
            return QVector<I *>();
        }

        T *m_widget = nullptr;
    };

    template <typename T, typename I>
    NavigationModeWrapper<T, I>::NavigationModeWrapper(T *p_widget)
        : NavigationMode(NavigationMode::Type::DoubleKeys, p_widget),
          m_widget(p_widget)
    {
    }

    template <typename T, typename I>
    QVector<void *> NavigationModeWrapper<T, I>::getVisibleNavigationItems()
    {
        QVector<void *> items;
        auto rawItems = getVisibleItems();
        items.reserve(rawItems.size());
        for (auto it : rawItems) {
            items.push_back(it);
        }
        return items;
    }

    template <typename T, typename I>
    void NavigationModeWrapper<T, I>::placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label)
    {
        Q_UNUSED(p_idx);
        Q_ASSERT(p_item);

        int extraWidth = p_label->width() + 2;
        auto vbar = m_widget->verticalScrollBar();
        if (vbar && vbar->minimum() != vbar->maximum()) {
            extraWidth += vbar->width();
        }

        auto item = static_cast<I *>(p_item);
        const auto rt = m_widget->visualItemRect(item);
        const int x = rt.x() + m_widget->width() - extraWidth;
        const int y = rt.y();
        p_label->move(x, y);
    }

    template <typename T, typename I>
    void NavigationModeWrapper<T, I>::handleTargetHit(void *p_item)
    {
        Q_ASSERT(p_item);
        auto item = static_cast<I *>(p_item);
        m_widget->setCurrentItem(item);
        m_widget->setFocus();
    }

    template <>
    inline QVector<QTreeWidgetItem *> NavigationModeWrapper<QTreeWidget, QTreeWidgetItem>::getVisibleItems() const
    {
        return TreeWidget::getVisibleItems(m_widget);
    }

    template <>
    inline QVector<QListWidgetItem *> NavigationModeWrapper<QListWidget, QListWidgetItem>::getVisibleItems() const
    {
        return ListWidget::getVisibleItems(m_widget);
    }
}

#endif // NAVIGATIONMODEWRAPPER_H
