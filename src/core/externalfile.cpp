#include "externalfile.h"

#include <utils/fileutils.h>
#include <utils/pathutils.h>

using namespace vnotex;

ExternalFile::ExternalFile(const QString &p_filePath)
    : c_filePath(p_filePath)
{
    setContentType(FileTypeHelper::getInst().getFileType(c_filePath).m_type);
}

QString ExternalFile::read() const
{
    return FileUtils::readTextFile(getContentPath());
}

void ExternalFile::write(const QString &p_content)
{
    FileUtils::writeFile(getContentPath(), p_content);
}

QString ExternalFile::getName() const
{
    return PathUtils::fileName(c_filePath);
}

QString ExternalFile::getFilePath() const
{
    return c_filePath;
}

QString ExternalFile::getContentPath() const
{
    return c_filePath;
}

QString ExternalFile::getResourcePath() const
{
    return PathUtils::parentDirPath(getContentPath());
}

IFileWithImage *ExternalFile::getImageInterface()
{
    return this;
}

Node *ExternalFile::getNode() const
{
    return nullptr;
}

QString ExternalFile::fetchImageFolderPath()
{
    auto pa = PathUtils::concatenateFilePath(getResourcePath(), QStringLiteral("vx_images"));
    QDir().mkpath(pa);
    return pa;
}

QString ExternalFile::insertImage(const QString &p_srcImagePath, const QString &p_imageFileName)
{
    const auto imageFolderPath = fetchImageFolderPath();
    auto destFilePath = FileUtils::renameIfExistsCaseInsensitive(PathUtils::concatenateFilePath(imageFolderPath, p_imageFileName));
    FileUtils::copyFile(p_srcImagePath, destFilePath);
    return destFilePath;
}

QString ExternalFile::insertImage(const QImage &p_image, const QString &p_imageFileName)
{
    const auto imageFolderPath = fetchImageFolderPath();
    auto destFilePath = FileUtils::renameIfExistsCaseInsensitive(PathUtils::concatenateFilePath(imageFolderPath, p_imageFileName));
    p_image.save(destFilePath);
    return destFilePath;
}

void ExternalFile::removeImage(const QString &p_imagePath)
{
    FileUtils::removeFile(p_imagePath);
}
