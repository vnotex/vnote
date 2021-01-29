#include "vxnode.h"

#include <QDir>

#include <utils/pathutils.h>
#include <notebookbackend/inotebookbackend.h>
#include <notebookconfigmgr/inotebookconfigmgr.h>
#include "notebook.h"
#include "vxnodefile.h"

using namespace vnotex;

VXNode::VXNode(ID p_id,
               const QString &p_name,
               const QDateTime &p_createdTimeUtc,
               const QDateTime &p_modifiedTimeUtc,
               const QStringList &p_tags,
               const QString &p_attachmentFolder,
               Notebook *p_notebook,
               Node *p_parent)
    : Node(Node::Flag::Content,
           p_id,
           p_name,
           p_createdTimeUtc,
           p_modifiedTimeUtc,
           p_tags,
           p_attachmentFolder,
           p_notebook,
           p_parent)
{
}

VXNode::VXNode(const QString &p_name,
               Notebook *p_notebook,
               Node *p_parent)
    : Node(Node::Flag::Container,
           p_name,
           p_notebook,
           p_parent)
{
}

QString VXNode::fetchAbsolutePath() const
{
    return PathUtils::concatenateFilePath(m_notebook->getRootFolderAbsolutePath(),
                                          fetchPath());
}

QSharedPointer<File> VXNode::getContentFile()
{
    // We should not keep the shared ptr of VXNodeFile, or there is a cyclic ref.
    return QSharedPointer<VXNodeFile>::create(sharedFromThis().dynamicCast<VXNode>());
}

QStringList VXNode::addAttachment(const QString &p_destFolderPath, const QStringList &p_files)
{
    Q_ASSERT(PathUtils::pathContains(fetchAttachmentFolderPath(), p_destFolderPath));

    auto backend = getBackend();
    QStringList addedFiles;
    for (const auto &file : p_files) {
        if (PathUtils::isDir(file)) {
            qWarning() << "skip adding folder as attachment" << file;
            continue;
        }

        auto destFilePath = backend->renameIfExistsCaseInsensitive(
            PathUtils::concatenateFilePath(p_destFolderPath, PathUtils::fileName(file)));
        backend->copyFile(file, destFilePath);
        addedFiles << destFilePath;
    }

    return addedFiles;
}

QString VXNode::newAttachmentFile(const QString &p_destFolderPath, const QString &p_name)
{
    Q_ASSERT(PathUtils::pathContains(fetchAttachmentFolderPath(), p_destFolderPath));

    auto backend = getBackend();
    auto destFilePath = backend->renameIfExistsCaseInsensitive(
        PathUtils::concatenateFilePath(p_destFolderPath, p_name));
    backend->writeFile(destFilePath, QByteArray());
    return destFilePath;
}

QString VXNode::newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name)
{
    Q_ASSERT(PathUtils::pathContains(fetchAttachmentFolderPath(), p_destFolderPath));

    auto backend = getBackend();
    auto destFilePath = backend->renameIfExistsCaseInsensitive(
        PathUtils::concatenateFilePath(p_destFolderPath, p_name));
    backend->makePath(destFilePath);
    return destFilePath;
}

QString VXNode::renameAttachment(const QString &p_path, const QString &p_name)
{
    Q_ASSERT(PathUtils::pathContains(fetchAttachmentFolderPath(), p_path));
    getBackend()->renameFile(p_path, p_name);
    return p_name;
}

void VXNode::removeAttachment(const QStringList &p_paths)
{
    auto attaFolderPath = fetchAttachmentFolderPath();
    // Just move it to recycle bin but not added as a child node of recycle bin.
    for (const auto &pa : p_paths) {
        Q_ASSERT(PathUtils::pathContains(attaFolderPath, pa));
        if (QFileInfo(pa).isDir()) {
            m_notebook->moveDirToRecycleBin(pa);
        } else {
            m_notebook->moveFileToRecycleBin(pa);
        }
    }
}
