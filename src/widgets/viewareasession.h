#ifndef VIEWAREASESSION_H
#define VIEWAREASESSION_H

#include <QVector>
#include <QDataStream>
#include <QHash>

#include <core/global.h>

#include "viewwindowsession.h"


namespace vnotex
{
    struct ViewAreaSession
    {
        // A node for splitter and ViewSplit hirarchy.
        struct Node
        {
            enum Type
            {
                Splitter,
                ViewSplit,
                Empty
            };

            void clear();

            bool isEmpty() const;

            Type m_type = Type::Empty;

            Qt::Orientation m_orientation = Qt::Horizontal;

            ID m_viewSplitId = InvalidViewSplitId;

            QVector<Node> m_children;
        };

        struct Workspace
        {
            ID m_viewSplitId = InvalidViewSplitId;

            QVector<ViewWindowSession> m_viewWindows;

            int m_currentViewWindowIndex = 0;
        };

        QByteArray serialize() const;

        static ViewAreaSession deserialize(const QByteArray &p_data);

        Node m_root;

        QVector<Workspace> m_workspaces;
    };


    extern QDataStream &operator<<(QDataStream &p_ds, const ViewAreaSession::Node &p_node);
    extern QDataStream &operator>>(QDataStream &p_ds, ViewAreaSession::Node &p_node);

    extern QDataStream &operator<<(QDataStream &p_ds, const ViewAreaSession::Workspace &p_workspace);
    extern QDataStream &operator>>(QDataStream &p_ds, ViewAreaSession::Workspace &p_workspace);
}

#endif // VIEWAREASESSION_H
