#ifndef VIEWWINDOWSESSION_H
#define VIEWWINDOWSESSION_H

#include <QDataStream>

#include <core/global.h>

namespace vnotex
{
    struct ViewWindowSession
    {
        QString m_bufferPath;

        bool m_readOnly = false;

        ViewWindowMode m_viewWindowMode = ViewWindowMode::Read;

        // 0-based.
        int m_lineNumber = -1;
    };

    extern QDataStream &operator<<(QDataStream &p_ds, const ViewWindowSession &p_session);
    extern QDataStream &operator>>(QDataStream &p_ds, ViewWindowSession &p_session);
}

#endif // VIEWWINDOWSESSION_H
