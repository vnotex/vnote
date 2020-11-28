#include "nodebufferprovider.h"

#include <QFileInfo>

#include <notebook/node.h>
#include <utils/pathutils.h>

using namespace vnotex;

NodeBufferProvider::NodeBufferProvider(Node *p_node, QObject *p_parent)
    : BufferProvider(p_parent),
      m_node(p_node),
      m_path(m_node->fetchAbsolutePath()),
      m_contentPath(m_node->fetchContentPath())
{
}

Buffer::ProviderType NodeBufferProvider::getType() const
{
    return Buffer::ProviderType::Internal;
}

bool NodeBufferProvider::match(const Node *p_node) const
{
    return m_node == p_node;
}

bool NodeBufferProvider::match(const QString &p_filePath) const
{
    return PathUtils::areSamePaths(getPath(), p_filePath);
}

QString NodeBufferProvider::getName() const
{
    return m_node->getName();
}

QString NodeBufferProvider::getPath() const
{
    return m_path;
}

QString NodeBufferProvider::getContentPath() const
{
    return m_contentPath;
}

void NodeBufferProvider::write(const QString &p_content)
{
    m_node->write(p_content);
    m_lastModified = getLastModifiedFromFile();
}

QString NodeBufferProvider::read() const
{
    const_cast<NodeBufferProvider *>(this)->m_lastModified = getLastModifiedFromFile();
    return m_node->read();
}

QString NodeBufferProvider::fetchImageFolderPath()
{
    return m_node->fetchImageFolderPath();
}

bool NodeBufferProvider::isChildOf(const Node *p_node) const
{
    return Node::isAncestor(p_node, m_node);
}

QString NodeBufferProvider::getAttachmentFolder() const
{
    return m_node->getAttachmentFolder();
}

QString NodeBufferProvider::fetchAttachmentFolderPath()
{
    return m_node->fetchAttachmentFolderPath();
}

QStringList NodeBufferProvider::addAttachment(const QString &p_destFolderPath, const QStringList &p_files)
{
    return m_node->addAttachment(p_destFolderPath, p_files);
}

QString NodeBufferProvider::newAttachmentFile(const QString &p_destFolderPath, const QString &p_name)
{
    return m_node->newAttachmentFile(p_destFolderPath, p_name);
}

QString NodeBufferProvider::newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name)
{
    return m_node->newAttachmentFolder(p_destFolderPath, p_name);
}

QString NodeBufferProvider::renameAttachment(const QString &p_path, const QString &p_name)
{
    return m_node->renameAttachment(p_path, p_name);
}

void NodeBufferProvider::removeAttachment(const QStringList &p_paths)
{
    return m_node->removeAttachment(p_paths);
}

QString NodeBufferProvider::insertImage(const QString &p_srcImagePath, const QString &p_imageFileName)
{
    return m_node->insertImage(p_srcImagePath, p_imageFileName);
}

QString NodeBufferProvider::insertImage(const QImage &p_image, const QString &p_imageFileName)
{
    return m_node->insertImage(p_image, p_imageFileName);
}

void NodeBufferProvider::removeImage(const QString &p_imagePath)
{
    m_node->removeImage(p_imagePath);
}

bool NodeBufferProvider::isAttachmentSupported() const
{
    return true;
}

Node *NodeBufferProvider::getNode() const
{
    return m_node;
}

bool NodeBufferProvider::isReadOnly() const
{
    return m_node->isReadOnly();
}
