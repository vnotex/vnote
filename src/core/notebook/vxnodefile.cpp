#include "vxnodefile.h"

#include <QImage>

#include <notebookbackend/inotebookbackend.h>
#include <notebookconfigmgr/inotebookconfigmgr.h>
#include <notebookconfigmgr/vxnotebookconfigmgr.h>
#include <utils/pathutils.h>
#include "vxnode.h"
#include "notebook.h"

using namespace vnotex;

VXNodeFile::VXNodeFile(const QSharedPointer<VXNode> &p_node)
    : m_node(p_node)
{
    Q_ASSERT(m_node && m_node->hasContent());
}

QString VXNodeFile::read() const
{
    return m_node->getBackend()->readTextFile(m_node->fetchPath());
}

void VXNodeFile::write(const QString &p_content)
{
    m_node->getBackend()->writeFile(m_node->fetchPath(), p_content);

    m_node->setModifiedTimeUtc();
    m_node->save();
}

QString VXNodeFile::getName() const
{
    return m_node->getName();
}

QString VXNodeFile::getFilePath() const
{
    return m_node->fetchAbsolutePath();
}

QString VXNodeFile::getContentPath() const
{
    return m_node->fetchAbsolutePath();
}

QString VXNodeFile::getResourcePath() const
{
    return PathUtils::parentDirPath(getContentPath());
}

IFileWithImage *VXNodeFile::getImageInterface()
{
    return this;
}

Node *VXNodeFile::getNode() const
{
    return m_node.data();
}

QString VXNodeFile::fetchImageFolderPath()
{
    auto configMgr = dynamic_cast<VXNotebookConfigMgr *>(m_node->getConfigMgr());
    return configMgr->fetchNodeImageFolderPath(m_node.data());
}

QString VXNodeFile::insertImage(const QString &p_srcImagePath, const QString &p_imageFileName)
{
    auto backend = m_node->getBackend();
    const auto imageFolderPath = fetchImageFolderPath();
    auto destFilePath = backend->renameIfExistsCaseInsensitive(PathUtils::concatenateFilePath(imageFolderPath, p_imageFileName));
    backend->copyFile(p_srcImagePath, destFilePath);
    return destFilePath;
}

QString VXNodeFile::insertImage(const QImage &p_image, const QString &p_imageFileName)
{
    auto backend = m_node->getBackend();
    const auto imageFolderPath = fetchImageFolderPath();
    auto destFilePath = backend->renameIfExistsCaseInsensitive(PathUtils::concatenateFilePath(imageFolderPath, p_imageFileName));
    p_image.save(destFilePath);
    backend->addFile(destFilePath);
    return destFilePath;
}

void VXNodeFile::removeImage(const QString &p_imagePath)
{
    // Just move it to recycle bin but not added as a child node of recycle bin.
    m_node->getNotebook()->moveFileToRecycleBin(p_imagePath);
}
