#include "foldernode.h"

using namespace vnotex;

FolderNode::FolderNode(const QString &p_name,
                       Notebook *p_notebook,
                       Node *p_parent)
    : Node(Node::Type::Folder,
           p_name,
           p_notebook,
           p_parent)
{
}

void FolderNode::loadFolder(ID p_id,
                            const QDateTime &p_createdTimeUtc,
                            const QVector<QSharedPointer<Node>> &p_children)
{
    Node::loadInfo(p_id, p_createdTimeUtc);
    m_children = p_children;
}

QVector<QSharedPointer<Node>> FolderNode::getChildren() const
{
    return m_children;
}

int FolderNode::getChildrenCount() const
{
    return m_children.size();
}

void FolderNode::addChild(const QSharedPointer<Node> &p_node)
{
    insertChild(m_children.size(), p_node);
}

void FolderNode::insertChild(int p_idx, const QSharedPointer<Node> &p_node)
{
    p_node->setParent(this);

    m_children.insert(p_idx, p_node);
}

void FolderNode::removeChild(const QSharedPointer<Node> &p_child)
{
    if (m_children.removeOne(p_child)) {
        p_child->setParent(nullptr);
    }
}

QDateTime FolderNode::getModifiedTimeUtc() const
{
    return getCreatedTimeUtc();
}

void FolderNode::setModifiedTimeUtc()
{
    Q_ASSERT(false);
}

QDir FolderNode::toDir() const
{
    return QDir(fetchAbsolutePath());
}
