#ifndef BUFFERMGR_H
#define BUFFERMGR_H

#include <QObject>
#include <QScopedPointer>
#include <QSharedPointer>
#include <QVector>

#include "namebasedserver.h"

namespace vnotex
{
    class IBufferFactory;
    class Node;
    class Buffer;
    struct FileOpenParameters;

    class BufferMgr : public QObject
    {
        Q_OBJECT
    public:
        explicit BufferMgr(QObject *p_parent = nullptr);

        ~BufferMgr();

        void init();

    public slots:
        void open(Node *p_node, const QSharedPointer<FileOpenParameters> &p_paras);

        void open(const QString &p_filePath, const QSharedPointer<FileOpenParameters> &p_paras);

    signals:
        void bufferRequested(Buffer *p_buffer, const QSharedPointer<FileOpenParameters> &p_paras);

    private:
        void initBufferServer();

        Buffer *findBuffer(const Node *p_node) const;

        Buffer *findBuffer(const QString &p_filePath) const;

        void addBuffer(Buffer *p_buffer);

        // Try to load @p_path as a node if it is within one notebook.
        QSharedPointer<Node> loadNodeByPath(const QString &p_path);

        QSharedPointer<NameBasedServer<IBufferFactory>> m_bufferServer;

        // Managed by QObject.
        QVector<Buffer *> m_buffers;
    };
} // ns vnotex

#endif // BUFFERMGR_H
