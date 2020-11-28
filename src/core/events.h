#ifndef EVENTS_H
#define EVENTS_H

#include <QVariant>

namespace vnotex
{
    class Event
    {
    public:
        void reset()
        {
            m_handled = false;
            m_response.clear();
        }

        // Whether this event is handled.
        // If it is handled, later handler should just ignore this event.
        bool m_handled = false;

        // Handler could use this field to return state to the event sender.
        QVariant m_response = true;
    };
}

#endif // EVENTS_H
