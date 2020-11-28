#ifndef FOLDERNODE_H
#define FOLDERNODE_H

#include "node.h"

namespace vnotex
{
    class FolderNode : public Node
    {
    public:
        FolderNode(const QString &p_name,
                   Notebook *p_notebook,
                   Node *p_parent = nullptr);

        void loadFolder(ID p_id,
                        const QDateTime &p_createdTimeUtc,
                        const QVector<QSharedPointer<Node>> &p_children);

        QVector<QSharedPointer<Node>> getChildren() const Q_DECL_OVERRIDE;

        int getChildrenCount() const Q_DECL_OVERRIDE;

        void addChild(const QSharedPointer<Node> &p_node) Q_DECL_OVERRIDE;

        void insertChild(int p_idx, const QSharedPointer<Node> &p_node) Q_DECL_OVERRIDE;

        void removeChild(const QSharedPointer<Node> &p_child) Q_DECL_OVERRIDE;

        QDateTime getModifiedTimeUtc() const Q_DECL_OVERRIDE;

        void setModifiedTimeUtc() Q_DECL_OVERRIDE;

        QDir toDir() const Q_DECL_OVERRIDE;

    private:
        QVector<QSharedPointer<Node>> m_children;
    };
} // ns vnotex

#endif // FOLDERNODE_H
