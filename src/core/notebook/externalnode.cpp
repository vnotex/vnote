#include "externalnode.h"

#include "node.h"
#include <utils/pathutils.h>

using namespace vnotex;

ExternalNode::ExternalNode(Node *p_parent, const QString &p_name, Type p_type)
    : m_parentNode(p_parent),
      m_name(p_name),
      m_type(p_type)
{
    Q_ASSERT(m_parentNode);
}

Node *ExternalNode::getNode() const
{
    return m_parentNode;
}

const QString &ExternalNode::getName() const
{
    return m_name;
}

bool ExternalNode::isFolder() const
{
    return m_type == Type::Folder;
}

QString ExternalNode::fetchAbsolutePath() const
{
    return PathUtils::concatenateFilePath(m_parentNode->fetchAbsolutePath(), m_name);
}
