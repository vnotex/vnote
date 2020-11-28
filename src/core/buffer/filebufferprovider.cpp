#include "filebufferprovider.h"

#include <QFileInfo>

#include <utils/pathutils.h>
#include <utils/fileutils.h>
#include <notebook/node.h>

using namespace vnotex;

FileBufferProvider::FileBufferProvider(const QString &p_filePath,
                                       Node *p_nodeAttachedTo,
                                       bool p_readOnly,
                                       QObject *p_parent)
    : BufferProvider(p_parent),
      c_filePath(p_filePath),
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
    return PathUtils::areSamePaths(c_filePath, p_filePath);
}

QString FileBufferProvider::getName() const
{
    return PathUtils::fileName(c_filePath);
}

QString FileBufferProvider::getPath() const
{
    return c_filePath;
}

QString FileBufferProvider::getContentPath() const
{
    // TODO.
    return getPath();
}

void FileBufferProvider::write(const QString &p_content)
{
    FileUtils::writeFile(getContentPath(), p_content);
    m_lastModified = getLastModifiedFromFile();
}

QString FileBufferProvider::read() const
{
    const_cast<FileBufferProvider *>(this)->m_lastModified = getLastModifiedFromFile();
    return FileUtils::readTextFile(getContentPath());
}

QString FileBufferProvider::fetchImageFolderPath()
{
    auto pa = PathUtils::concatenateFilePath(PathUtils::parentDirPath(getContentPath()), QStringLiteral("vx_images"));
    QDir().mkpath(pa);
    return pa;
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
    const auto imageFolderPath = fetchImageFolderPath();
    auto destFilePath = FileUtils::renameIfExistsCaseInsensitive(PathUtils::concatenateFilePath(imageFolderPath, p_imageFileName));
    FileUtils::copyFile(p_srcImagePath, destFilePath);
    return destFilePath;
}

QString FileBufferProvider::insertImage(const QImage &p_image, const QString &p_imageFileName)
{
    const auto imageFolderPath = fetchImageFolderPath();
    auto destFilePath = FileUtils::renameIfExistsCaseInsensitive(PathUtils::concatenateFilePath(imageFolderPath, p_imageFileName));
    p_image.save(destFilePath);
    return destFilePath;
}

void FileBufferProvider::removeImage(const QString &p_imagePath)
{
    FileUtils::removeFile(p_imagePath);
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
