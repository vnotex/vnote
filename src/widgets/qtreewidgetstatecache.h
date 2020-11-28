#ifndef QTREEWIDGETSTATECACHE_H
#define QTREEWIDGETSTATECACHE_H

#include <functional>

#include <QSet>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace vnotex
{
    template<class Key>
    class QTreeWidgetStateCache
    {
    public:
        typedef std::function<Key(const QTreeWidgetItem *, bool &)> ItemKeyFunc;

        explicit QTreeWidgetStateCache(const ItemKeyFunc &p_keyFunc)
            : m_keyFunc(p_keyFunc),
              m_currentItem(0)
        {
        }

        void save(QTreeWidget *p_tree, bool p_saveCurrentItem)
        {
            clear();

            auto cnt = p_tree->topLevelItemCount();
            for (int i = 0; i < cnt; ++i) {
                save(p_tree->topLevelItem(i));
            }

            if (p_saveCurrentItem) {
                auto item = p_tree->currentItem();
                bool ok;
                Key key = m_keyFunc(item, ok);
                if (ok) {
                    m_currentItem = key;
                }
            }
        }

        bool contains(QTreeWidgetItem *p_item) const
        {
            bool ok;
            Key key = m_keyFunc(p_item, ok);
            if (ok) {
                return m_expansionCache.contains(key);
            }

            return false;
        }

        void clear()
        {
            m_expansionCache.clear();
            m_currentItem = 0;
        }

        Key getCurrentItem() const
        {
            return m_currentItem;
        }

    private:
        void save(QTreeWidgetItem *p_item)
        {
            if (!p_item->isExpanded()) {
                return;
            }

            bool ok;
            Key key = m_keyFunc(p_item, ok);
            if (ok) {
                m_expansionCache.insert(key);
            }

            auto cnt = p_item->childCount();
            for (int i = 0; i < cnt; ++i) {
                save(p_item->child(i));
            }
        }

        QSet<Key> m_expansionCache;

        ItemKeyFunc m_keyFunc;

        Key m_currentItem;
    };
} // ns vnotex

#endif // QTREEWIDGETSTATECACHE_H
