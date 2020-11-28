#ifndef FILELOCATOR_H
#define FILELOCATOR_H

#include <QString>

namespace vnotex
{
    class Node;

    // A unique locator for both internal Node and external file.
    class FileLocator
    {
    public:
        FileLocator(Node *p_node)
            : m_node(p_node)
        {
        }

        FileLocator(const QString &p_filePath)
            : m_filePath(p_filePath)
        {
        }

        bool isNode() const
        {
            return m_node;
        }

        Node *node() const
        {
            Q_ASSERT(isNode());
            return m_node;
        }

        const QString &filePath() const
        {
            Q_ASSERT(!isNode());
            return m_filePath;
        }

    private:
        Node *m_node = nullptr;

        QString m_filePath;
    };
}

#endif // FILELOCATOR_H
