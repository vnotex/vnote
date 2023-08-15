#include "notebookexplorersession.h"
#include <QIODevice>

using namespace vnotex;

QDataStream &::vnotex::operator<<(QDataStream &p_ds, const NotebookSession &p_session)
{
    p_ds << p_session.m_currentNodePath;
    return p_ds;
}

QDataStream &::vnotex::operator>>(QDataStream &p_ds, NotebookSession &p_session)
{
    p_ds >> p_session.m_currentNodePath;
    return p_ds;
}

QByteArray NotebookExplorerSession::serialize() const
{
    QByteArray data;
    QDataStream outs(&data, QIODevice::WriteOnly);
    outs.setVersion(QDataStream::Qt_5_12);

    outs << m_notebooks;

    return data;
}

NotebookExplorerSession NotebookExplorerSession::deserialize(const QByteArray &p_data)
{
    NotebookExplorerSession session;
    if (p_data.isEmpty()) {
        return session;
    }

    QDataStream ins(p_data);
    ins.setVersion(QDataStream::Qt_5_12);

    ins >> session.m_notebooks;

    return session;
}
