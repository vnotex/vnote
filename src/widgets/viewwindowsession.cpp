#include "viewwindowsession.h"

using namespace vnotex;

QDataStream &::vnotex::operator<<(QDataStream &p_ds, const ViewWindowSession &p_session)
{
    p_ds << p_session.m_bufferPath;
    p_ds << static_cast<qint8>(p_session.m_readOnly);
    p_ds << static_cast<qint8>(p_session.m_viewWindowMode);
    p_ds << static_cast<qint32>(p_session.m_lineNumber);
    return p_ds;
}

QDataStream &::vnotex::operator>>(QDataStream &p_ds, ViewWindowSession &p_session)
{
    p_ds >> p_session.m_bufferPath;

    qint8 tmp8 = 0;

    p_ds >> tmp8;
    p_session.m_readOnly = tmp8 > 0;

    p_ds >> tmp8;
    p_session.m_viewWindowMode = static_cast<ViewWindowMode>(tmp8);

    {
        qint32 tmp = 0;
        p_ds >> tmp;
        p_session.m_lineNumber = static_cast<int>(tmp);
    }

    return p_ds;
}
