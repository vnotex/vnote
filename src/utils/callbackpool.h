#ifndef CALLBACKPOOL_H
#define CALLBACKPOOL_H

#include <functional>

#include <QMap>

namespace vnotex
{
    // Manage callbacks with id.
    class CallbackPool
    {
    public:
        typedef std::function<void(void *)> Callback;

        CallbackPool() = default;

        quint64 add(const Callback &p_callback);

        void call(quint64 p_id, void *p_data);

        void clear();

    private:
        QMap<quint64, Callback> m_pool;
    };
}

#endif // CALLBACKPOOL_H
