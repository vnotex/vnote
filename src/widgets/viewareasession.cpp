#include "viewareasession.h"

#include <QIODevice>
#include <QDebug>
#include <QSplitter>
#include <QLayout>

using namespace vnotex;

QDataStream &::vnotex::operator<<(QDataStream &p_ds, const ViewAreaSession::Node &p_node)
{
    p_ds << static_cast<qint8>(p_node.m_type);
    p_ds << static_cast<qint8>(p_node.m_orientation);
    p_ds << p_node.m_viewSplitId;
    p_ds << p_node.m_children;
    return p_ds;
}

QDataStream &::vnotex::operator>>(QDataStream &p_ds, ViewAreaSession::Node &p_node)
{
    qint8 tmp = 0;

    p_ds >> tmp;
    p_node.m_type = static_cast<ViewAreaSession::Node::Type>(tmp);

    p_ds >> tmp;
    p_node.m_orientation = static_cast<Qt::Orientation>(tmp);

    p_ds >> p_node.m_viewSplitId;
    p_ds >> p_node.m_children;
    return p_ds;
}

QDataStream &::vnotex::operator<<(QDataStream &p_ds, const ViewAreaSession::Workspace &p_workspace)
{
    p_ds << p_workspace.m_viewSplitId;

    p_ds << p_workspace.m_viewWindows;

    p_ds << static_cast<qint32>(p_workspace.m_currentViewWindowIndex);

    return p_ds;
}

QDataStream &::vnotex::operator>>(QDataStream &p_ds, ViewAreaSession::Workspace &p_workspace)
{
    p_ds >> p_workspace.m_viewSplitId;

    p_ds >> p_workspace.m_viewWindows;

    {
        qint32 tmp = 0;
        p_ds >> tmp;
        p_workspace.m_currentViewWindowIndex = tmp;
    }

    return p_ds;
}

void ViewAreaSession::Node::clear()
{
    m_type = Type::Empty;
    m_orientation = Qt::Horizontal;
    m_viewSplitId = InvalidViewSplitId;
    m_children.clear();
}

bool ViewAreaSession::Node::isEmpty() const
{
    return m_type == Type::Empty;
}

QByteArray ViewAreaSession::serialize() const
{
    QByteArray data;
    QDataStream outs(&data, QIODevice::WriteOnly);
    outs.setVersion(QDataStream::Qt_5_12);

    outs << m_root;

    outs << m_workspaces;

    return data;
}

ViewAreaSession ViewAreaSession::deserialize(const QByteArray &p_data)
{
    ViewAreaSession session;
    if (p_data.isEmpty()) {
        return session;
    }

    QDataStream ins(p_data);
    ins.setVersion(QDataStream::Qt_5_12);

    ins >> session.m_root;

    ins >> session.m_workspaces;

    return session;
}
