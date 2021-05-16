#ifndef NOTEBOOKEXPLORERSESSION_H
#define NOTEBOOKEXPLORERSESSION_H

#include <QDataStream>
#include <QHash>

namespace vnotex
{
    struct NotebookSession
    {
        // Used to judge whether this session has been recovered.
        bool m_recovered = false;

        QString m_currentNodePath;
    };

    class NotebookExplorerSession
    {
    public:
        QByteArray serialize() const;

        static NotebookExplorerSession deserialize(const QByteArray &p_data);

        // Notebook's path to its session.
        QHash<QString, NotebookSession> m_notebooks;
    };

    extern QDataStream &operator<<(QDataStream &p_ds, const NotebookSession &p_session);
    extern QDataStream &operator>>(QDataStream &p_ds, NotebookSession &p_session);
}

#endif // NOTEBOOKEXPLORERSESSION_H
