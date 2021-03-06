#ifndef EXTERNALNODE_H
#define EXTERNALNODE_H

#include <QString>

namespace vnotex
{
    class Node;

    // External node not managed by VNote.
    class ExternalNode
    {
    public:
        enum class Type
        {
            File,
            Folder
        };

        ExternalNode(Node *p_parent, const QString &p_name, Type p_type);

        Node *getNode() const;

        const QString &getName() const;

        bool isFolder() const;

        QString fetchAbsolutePath() const;

    private:
        // Parent node.
        // We support only one level further the external folder.
        Node *m_parentNode = nullptr;

        QString m_name;

        Type m_type = Type::File;
    };
}

#endif // EXTERNALNODE_H
