#include "callbackpool.h"

#include <QDebug>

using namespace vnotex;

quint64 CallbackPool::add(const Callback &p_callback)
{
    static quint64 nextId = 0;
    quint64 id = nextId++;
    m_pool.insert(id, p_callback);
    return id;
}

void CallbackPool::call(quint64 p_id, void *p_data)
{
    auto it = m_pool.find(p_id);
    if (it != m_pool.end()) {
        it.value()(p_data);
        m_pool.erase(it);
    } else {
        qWarning() << "failed to locate callback in pool with id" << p_id;
    }
}

void CallbackPool::clear()
{
    m_pool.clear();
}
