#ifndef URLDRAGDROPUTILS_H
#define URLDRAGDROPUTILS_H

#include <functional>

#include <QStringList>

class QDragEnterEvent;
class QDropEvent;

namespace vnotex
{
    // Help to handle Url-related Drag&Drop events.
    class UrlDragDropUtils
    {
    public:
        UrlDragDropUtils() = delete;

        static bool handleDragEnterEvent(QDragEnterEvent *p_event);

        static bool handleDropEvent(QDropEvent *p_event, std::function<void(const QStringList &)> p_func);
    };
}

#endif // URLDRAGDROPUTILS_H
