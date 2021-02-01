#include "filebufferprovider.h"

#include <QFileInfo>

#include <utils/pathutils.h>
#include <utils/fileutils.h>
#include <notebook/node.h>
#include <core/file.h>

using namespace vnotex;

FileBufferProvider::FileBufferProvider(const QSharedPointer<File> &p_file,
                                       Node *p_nodeAttachedTo,
                                       bool p_readOnly,
                                       QObject *p_parent)
    : BufferProvider(p_parent),
      m_file(p_file),
      c_nodeAttachedTo(p_nodeAttachedTo),
      m_readOnly(p_readOnly)
{
}

Buffer::ProviderType FileBufferProvider::getType() const
{
    return Buffer::ProviderType::External;
}

bool FileBufferProvider::match(const Node *p_node) const
{
    Q_UNUSED(p_node);
    return false;
}

bool FileBufferProvider::match(const QString &p_filePath) const
{
    return PathUtils::areSamePaths(m_file->getFilePath(), p_filePath);
}

QString FileBufferProvider::getName() const
{
    return m_file->getName();
}

QString FileBufferProvider::getPath() const
{
    return m_file->getFilePath();
}

QString FileBufferProvider::getContentPath() const
{
    return m_file->getContentPath();
}

QString FileBufferProvider::getResourcePath() const
{
    return m_file->getResourcePath();
}

void FileBufferProvider::write(const QString &p_content)
{
    m_file->write(p_content);
    m_lastModified = getLastModifiedFromFile();
}

QString FileBufferProvider::read() const
{
    const_cast<FileBufferProvider *>(this)->m_lastModified = getLastModifiedFromFile();
    return m_file->read();
}

QString FileBufferProvider::fetchImageFolderPath()
{
    auto file = m_file->getImageInterface();
    if (file) {
        return file->fetchImageFolderPath();
    } else {
        return QString();
    }
}

bool FileBufferProvider::isChildOf(const Node *p_node) const
{
    if (c_nodeAttachedTo) {
        return c_nodeAttachedTo == p_node || Node::isAncestor(p_node, c_nodeAttachedTo);
    }
    return false;
}

QString FileBufferProvider::getAttachmentFolder() const
{
    Q_ASSERT(false);
    return QString();
}

QString FileBufferProvider::fetchAttachmentFolderPath()
{
    Q_ASSERT(false);
    return QString();
}

QStringList FileBufferProvider::addAttachment(const QString &p_destFolderPath, const QStringList &p_files)
{
    Q_UNUSED(p_destFolderPath);
    Q_UNUSED(p_files);
    Q_ASSERT(false);
    return QStringList();
}

QString FileBufferProvider::newAttachmentFile(const QString &p_destFolderPath, const QString &p_name)
{
    Q_UNUSED(p_destFolderPath);
    Q_UNUSED(p_name);
    Q_ASSERT(false);
    return QString();
}

QString FileBufferProvider::newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name)
{
    Q_UNUSED(p_destFolderPath);
    Q_UNUSED(p_name);
    Q_ASSERT(false);
    return QString();
}

QString FileBufferProvider::renameAttachment(const QString &p_path, const QString &p_name)
{
    Q_UNUSED(p_path);
    Q_UNUSED(p_name);
    Q_ASSERT(false);
    return QString();
}

void FileBufferProvider::removeAttachment(const QStringList &p_paths)
{
    Q_UNUSED(p_paths);
    Q_ASSERT(false);
}

QString FileBufferProvider::insertImage(const QString &p_srcImagePath, const QString &p_imageFileName)
{
    auto file = m_file->getImageInterface();
    if (file) {
        return file->insertImage(p_srcImagePath, p_imageFileName);
    } else {
        return QString();
    }
}

QString FileBufferProvider::insertImage(const QImage &p_image, const QString &p_imageFileName)
{
    auto file = m_file->getImageInterface();
    if (file) {
        return file->insertImage(p_image, p_imageFileName);
    } else {
        return QString();
    }
}

void FileBufferProvider::removeImage(const QString &p_imagePath)
{
    auto file = m_file->getImageInterface();
    if (file) {
        file->removeImage(p_imagePath);
    }
}

bool FileBufferProvider::isAttachmentSupported() const
{
    return false;
}

Node *FileBufferProvider::getNode() const
{
    return c_nodeAttachedTo;
}

bool FileBufferProvider::isReadOnly() const
{
    return m_readOnly;
}
