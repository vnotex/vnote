#ifndef NAMEBASEDSERVER_H
#define NAMEBASEDSERVER_H

#include <QHash>
#include <QSharedPointer>
#include <QDebug>
#include <QList>

namespace vnotex
{
    template <typename T>
    class NameBasedServer
    {
    public:
        // Register an item.
        bool registerItem(const QString &p_name, const QSharedPointer<T> &p_item)
        {
            if (m_data.contains(p_name)) {
                qWarning() << "item to register already exists with name" << p_name;
                return false;
            }

            m_data.insert(p_name, p_item);
            return true;
        }

        // Get an item.
        QSharedPointer<T> getItem(const QString &p_name)
        {
            auto it = m_data.find(p_name);
            if (it != m_data.end()) {
                return it.value();
            }

            return nullptr;
        }

        QList<QSharedPointer<T>> getAllItems() const
        {
            return m_data.values();
        }

    private:
        // Name to item mapping.
        QHash<QString, QSharedPointer<T>> m_data;
    };
} // ns vnotex


#endif // NAMEBASEDSERVER_H
