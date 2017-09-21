#include "vnotefile.h"

#include <QDir>
#include <QDebug>
#include <QTextEdit>
#include <QFileInfo>
#include <QDebug>

#include "utils/vutils.h"
#include "vdirectory.h"

VNoteFile::VNoteFile(VDirectory *p_directory,
                     const QString &p_name,
                     FileType p_type,
                     bool p_modifiable,
                     QDateTime p_createdTimeUtc,
                     QDateTime p_modifiedTimeUtc)
    : VFile(p_directory, p_name, p_type, p_modifiable, p_createdTimeUtc, p_modifiedTimeUtc)
{
}

QString VNoteFile::fetchPath() const
{
    return QDir(getDirectory()->fetchPath()).filePath(m_name);
}

QString VNoteFile::fetchBasePath() const
{
    return getDirectory()->fetchPath();
}

QString VNoteFile::fetchImageFolderPath() const
{
    return QDir(fetchBasePath()).filePath(getNotebook()->getImageFolder());
}

bool VNoteFile::useRelativeImageFolder() const
{
    // Always use relative image folder.
    return true;
}

QString VNoteFile::getImageFolderInLink() const
{
    return getNotebook()->getImageFolder();
}

void VNoteFile::setName(const QString &p_name)
{
    Q_ASSERT(m_name.isEmpty()
             || (m_docType == VUtils::docTypeFromName(p_name)));

    m_name = p_name;
}

bool VNoteFile::rename(const QString &p_name)
{
    if (m_name == p_name) {
        return true;
    }

    QString oldName = m_name;

    VDirectory *dir = getDirectory();
    Q_ASSERT(dir);

    // Rename it in disk.
    QDir diskDir(dir->fetchPath());
    if (!diskDir.rename(m_name, p_name)) {
        qWarning() << "fail to rename file" << m_name << "to" << p_name << "in disk";
        return false;
    }

    m_name = p_name;

    // Update parent directory's config file.
    if (!dir->writeToConfig()) {
        m_name = oldName;
        diskDir.rename(p_name, m_name);
        return false;
    }

    // Can't not change doc type.
    Q_ASSERT(m_docType == VUtils::docTypeFromName(m_name));

    qDebug() << "file renamed from" << oldName << "to" << m_name;
    return true;
}

VDirectory *VNoteFile::getDirectory()
{
    Q_ASSERT(parent());
    return (VDirectory *)parent();
}

const VDirectory *VNoteFile::getDirectory() const
{
    Q_ASSERT(parent());
    return (const VDirectory *)parent();
}

VNotebook *VNoteFile::getNotebook()
{
    return getDirectory()->getNotebook();
}

const VNotebook *VNoteFile::getNotebook() const
{
    return getDirectory()->getNotebook();
}

QString VNoteFile::getNotebookName() const
{
    return getDirectory()->getNotebookName();
}

QString VNoteFile::fetchRelativePath() const
{
    return QDir(getDirectory()->fetchRelativePath()).filePath(m_name);
}

VNoteFile *VNoteFile::fromJson(VDirectory *p_directory,
                               const QJsonObject &p_json,
                               FileType p_type,
                               bool p_modifiable)
{
    return new VNoteFile(p_directory,
                         p_json[DirConfig::c_name].toString(),
                         p_type,
                         p_modifiable,
                         QDateTime::fromString(p_json[DirConfig::c_createdTime].toString(),
                                               Qt::ISODate),
                         QDateTime::fromString(p_json[DirConfig::c_modifiedTime].toString(),
                                               Qt::ISODate));
}

QJsonObject VNoteFile::toConfigJson() const
{
    QJsonObject item;
    item[DirConfig::c_name] = m_name;
    item[DirConfig::c_createdTime] = m_createdTimeUtc.toString(Qt::ISODate);
    item[DirConfig::c_modifiedTime] = m_modifiedTimeUtc.toString(Qt::ISODate);

    return item;
}

bool VNoteFile::deleteFile()
{
    Q_ASSERT(parent());

    bool ret = false;

    // Delete local images if it is Markdown.
    if (m_docType == DocType::Markdown) {
        deleteInternalImages();
    }

    // TODO: Delete attachments.

    // Delete the file.
    QString filePath = fetchPath();
    if (VUtils::deleteFile(getNotebook(), filePath, false)) {
        ret = true;
        qDebug() << "deleted" << m_name << filePath;
    } else {
        qWarning() << "fail to delete" << m_name << filePath;
    }

    return ret;
}

void VNoteFile::deleteInternalImages()
{
    Q_ASSERT(parent() && m_docType == DocType::Markdown);

    QVector<ImageLink> images = VUtils::fetchImagesFromMarkdownFile(this,
                                                                    ImageLink::LocalRelativeInternal);
    int deleted = 0;
    for (int i = 0; i < images.size(); ++i) {
        if (VUtils::deleteFile(getNotebook(), images[i].m_path, false)) {
            ++deleted;
        }
    }

    qDebug() << "delete" << deleted << "images for" << m_name << fetchPath();
}

