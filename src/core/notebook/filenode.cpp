#include "filenode.h"

#include <notebookconfigmgr/inotebookconfigmgr.h>
#include <notebookbackend/inotebookbackend.h>
#include <utils/pathutils.h>
#include <utils/fileutils.h>
#include "notebook.h"

using namespace vnotex;

FileNode::FileNode(ID p_id,
                   const QString &p_name,
                   const QDateTime &p_createdTimeUtc,
                   const QDateTime &p_modifiedTimeUtc,
                   const QString &p_attachmentFolder,
                   const QStringList &p_tags,
                   Notebook *p_notebook,
                   Node *p_parent)
    : Node(Node::Type::File,
           p_id,
           p_name,
           p_createdTimeUtc,
           p_notebook,
           p_parent),
      m_modifiedTimeUtc(p_modifiedTimeUtc),
      m_attachmentFolder(p_attachmentFolder),
      m_tags(p_tags)
{
}

QVector<QSharedPointer<Node>> FileNode::getChildren() const
{
    return QVector<QSharedPointer<Node>>();
}

int FileNode::getChildrenCount() const
{
    return 0;
}

void FileNode::addChild(const QSharedPointer<Node> &p_node)
{
    Q_ASSERT(false);
    Q_UNUSED(p_node);
}

void FileNode::insertChild(int p_idx, const QSharedPointer<Node> &p_node)
{
    Q_ASSERT(false);
    Q_UNUSED(p_idx);
    Q_UNUSED(p_node);
}

void FileNode::removeChild(const QSharedPointer<Node> &p_child)
{
    Q_ASSERT(false);
    Q_UNUSED(p_child);
}

QDateTime FileNode::getModifiedTimeUtc() const
{
    return m_modifiedTimeUtc;
}

void FileNode::setModifiedTimeUtc()
{
    m_modifiedTimeUtc = QDateTime::currentDateTimeUtc();
}

QString FileNode::getAttachmentFolder() const
{
    return m_attachmentFolder;
}

void FileNode::setAttachmentFolder(const QString &p_folder)
{
    m_attachmentFolder = p_folder;
}

QStringList FileNode::addAttachment(const QString &p_destFolderPath, const QStringList &p_files)
{
    QStringList addedFiles;
    for (const auto &file : p_files) {
        if (PathUtils::isDir(file)) {
            qWarning() << "skip adding folder as attachment" << file;
            continue;
        }

        auto destFilePath = m_backend->renameIfExistsCaseInsensitive(
            PathUtils::concatenateFilePath(p_destFolderPath, PathUtils::fileName(file)));
        m_backend->copyFile(file, destFilePath);
        addedFiles << destFilePath;
    }

    return addedFiles;
}

QString FileNode::newAttachmentFile(const QString &p_destFolderPath, const QString &p_name)
{
    auto destFilePath = m_backend->renameIfExistsCaseInsensitive(
        PathUtils::concatenateFilePath(p_destFolderPath, p_name));
    m_backend->writeFile(destFilePath, QByteArray());
    return destFilePath;
}

QString FileNode::newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name)
{
    auto destFilePath = m_backend->renameIfExistsCaseInsensitive(
        PathUtils::concatenateFilePath(p_destFolderPath, p_name));
    m_backend->makePath(destFilePath);
    return destFilePath;
}

QString FileNode::renameAttachment(const QString &p_path, const QString &p_name)
{
    m_backend->renameFile(p_path, p_name);
    return p_name;
}

void FileNode::removeAttachment(const QStringList &p_paths)
{
    // Just move it to recycle bin but not added as a child node of recycle bin.
    for (const auto &pa : p_paths) {
        if (QFileInfo(pa).isDir()) {
            m_notebook->moveDirToRecycleBin(pa);
        } else {
            m_notebook->moveFileToRecycleBin(pa);
        }
    }
}

QStringList FileNode::getTags() const
{
    return m_tags;
}
