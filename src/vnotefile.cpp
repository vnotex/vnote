#include "vnotefile.h"

#include <QDir>
#include <QDebug>
#include <QTextEdit>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

#include "utils/vutils.h"
#include "vdirectory.h"

VNoteFile::VNoteFile(VDirectory *p_directory,
                     const QString &p_name,
                     FileType p_type,
                     bool p_modifiable,
                     QDateTime p_createdTimeUtc,
                     QDateTime p_modifiedTimeUtc,
                     const QString &p_attachmentFolder,
                     const QVector<VAttachment> &p_attachments)
    : VFile(p_directory, p_name, p_type, p_modifiable, p_createdTimeUtc, p_modifiedTimeUtc),
      m_attachmentFolder(p_attachmentFolder),
      m_attachments(p_attachments)
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
    if (!dir->updateFileConfig(this)) {
        m_name = oldName;
        diskDir.rename(p_name, m_name);
        return false;
    }

    // Can't not change doc type.
    Q_ASSERT(m_docType == DocType::Unknown
             || m_docType == VUtils::docTypeFromName(m_name));

    m_docType = VUtils::docTypeFromName(m_name);

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
    // Attachments.
    QJsonArray attachmentJson = p_json[DirConfig::c_attachments].toArray();
    QVector<VAttachment> attachments;
    for (int i = 0; i < attachmentJson.size(); ++i) {
        QJsonObject attachmentItem = attachmentJson[i].toObject();
        attachments.push_back(VAttachment(attachmentItem[DirConfig::c_name].toString()));
    }

    return new VNoteFile(p_directory,
                         p_json[DirConfig::c_name].toString(),
                         p_type,
                         p_modifiable,
                         QDateTime::fromString(p_json[DirConfig::c_createdTime].toString(),
                                               Qt::ISODate),
                         QDateTime::fromString(p_json[DirConfig::c_modifiedTime].toString(),
                                               Qt::ISODate),
                         p_json[DirConfig::c_attachmentFolder].toString(),
                         attachments);
}

QJsonObject VNoteFile::toConfigJson() const
{
    QJsonObject item;
    item[DirConfig::c_name] = m_name;
    item[DirConfig::c_createdTime] = m_createdTimeUtc.toString(Qt::ISODate);
    item[DirConfig::c_modifiedTime] = m_modifiedTimeUtc.toString(Qt::ISODate);
    item[DirConfig::c_attachmentFolder] = m_attachmentFolder;

    // Attachments.
    QJsonArray attachmentJson;
    for (int i = 0; i < m_attachments.size(); ++i) {
        const VAttachment &item = m_attachments[i];
        QJsonObject attachmentItem;
        attachmentItem[DirConfig::c_name] = item.m_name;
        attachmentJson.append(attachmentItem);
    }

    item[DirConfig::c_attachments] = attachmentJson;

    return item;
}

bool VNoteFile::deleteFile(QString *p_errMsg)
{
    Q_ASSERT(!m_opened);
    Q_ASSERT(parent());

    bool ret = true;

    // Delete local images if it is Markdown.
    if (m_docType == DocType::Markdown) {
        if (!deleteInternalImages()) {
            ret = false;
            VUtils::addErrMsg(p_errMsg, tr("Fail to delete images of this note."));
        }
    }

    // Delete attachments.
    if (!deleteAttachments()) {
        ret = false;
        VUtils::addErrMsg(p_errMsg, tr("Fail to delete attachments of this note."));
    }

    // Delete the file.
    QString filePath = fetchPath();
    if (VUtils::deleteFile(getNotebook(), filePath, false)) {
        qDebug() << "deleted" << m_name << filePath;
    } else {
        ret = false;
        VUtils::addErrMsg(p_errMsg, tr("Fail to delete the note file."));
        qWarning() << "fail to delete" << m_name << filePath;
    }

    return ret;
}

bool VNoteFile::deleteInternalImages()
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

    return deleted == images.size();
}

bool VNoteFile::addAttachment(const QString &p_file)
{
    if (p_file.isEmpty() || !QFileInfo::exists(p_file)) {
        return false;
    }

    QString folderPath = fetchAttachmentFolderPath();
    QString name = VUtils::fileNameFromPath(p_file);
    Q_ASSERT(!name.isEmpty());
    name = VUtils::getFileNameWithSequence(folderPath, name);
    QString destPath = QDir(folderPath).filePath(name);
    if (!VUtils::copyFile(p_file, destPath, false)) {
        return false;
    }

    m_attachments.push_back(VAttachment(name));

    if (!getDirectory()->updateFileConfig(this)) {
        qWarning() << "fail to update config of file" << m_name
                   << "in directory" << fetchBasePath();
        return false;
    }

    return true;
}

QString VNoteFile::fetchAttachmentFolderPath()
{
    QString folderPath = QDir(fetchBasePath()).filePath(getNotebook()->getAttachmentFolder());
    if (m_attachmentFolder.isEmpty()) {
        m_attachmentFolder = VUtils::getRandomFileName(folderPath);
    }

    folderPath = QDir(folderPath).filePath(m_attachmentFolder);
    if (!QFileInfo::exists(folderPath)) {
        QDir dir;
        if (!dir.mkpath(folderPath)) {
            qWarning() << "fail to create attachment folder of notebook" << m_name << folderPath;
        }
    }

    return folderPath;
}

bool VNoteFile::deleteAttachments()
{
    if (m_attachments.isEmpty()) {
        return true;
    }

    QVector<QString> attas;
    for (int i = 0; i < m_attachments.size(); ++i) {
        attas.push_back(m_attachments[i].m_name);
    }

    return deleteAttachments(attas);
}

bool VNoteFile::deleteAttachments(const QVector<QString> &p_names)
{
    if (p_names.isEmpty()) {
        return true;
    }

    QDir dir(fetchAttachmentFolderPath());
    bool ret = true;
    for (int i = 0; i < p_names.size(); ++i) {
        int idx = findAttachment(p_names[i]);
        if (idx == -1) {
            ret = false;
            continue;
        }

        m_attachments.remove(idx);
        if (!VUtils::deleteFile(getNotebook(), dir.filePath(p_names[i]), false)) {
            ret = false;
            qWarning() << "fail to delete attachment" << p_names[i]
                       << "for note" << m_name;
        }
    }

    // Delete the attachment folder if m_attachments is empty now.
    if (m_attachments.isEmpty()) {
        dir.cdUp();
        if (!dir.rmdir(m_attachmentFolder)) {
            ret = false;
            qWarning() << "fail to delete attachment folder" << m_attachmentFolder
                       << "for note" << m_name;
        }
    }

    if (!getDirectory()->updateFileConfig(this)) {
        ret = false;
        qWarning() << "fail to update config of file" << m_name
                   << "in directory" << fetchBasePath();
    }

    return ret;
}

int VNoteFile::findAttachment(const QString &p_name, bool p_caseSensitive)
{
    const QString name = p_caseSensitive ? p_name : p_name.toLower();
    for (int i = 0; i < m_attachments.size(); ++i) {
        QString attaName = p_caseSensitive ? m_attachments[i].m_name
                                           : m_attachments[i].m_name.toLower();
        if (name == attaName) {
            return i;
        }
    }

    return -1;
}

bool VNoteFile::sortAttachments(const QVector<int> &p_sortedIdx)
{
    V_ASSERT(m_opened);
    V_ASSERT(p_sortedIdx.size() == m_attachments.size());

    auto ori = m_attachments;

    for (int i = 0; i < p_sortedIdx.size(); ++i) {
        m_attachments[i] = ori[p_sortedIdx[i]];
    }

    bool ret = true;
    if (!getDirectory()->updateFileConfig(this)) {
        qWarning() << "fail to reorder attachments in config" << p_sortedIdx;
        m_attachments = ori;
        ret = false;
    }

    return ret;
}

bool VNoteFile::renameAttachment(const QString &p_oldName, const QString &p_newName)
{
    int idx = findAttachment(p_oldName);
    if (idx == -1) {
        return false;
    }

    QDir dir(fetchAttachmentFolderPath());
    if (!dir.rename(p_oldName, p_newName)) {
        qWarning() << "fail to rename attachment file" << p_oldName << p_newName;
        return false;
    }

    m_attachments[idx].m_name = p_newName;

    if (!getDirectory()->updateFileConfig(this)) {
        qWarning() << "fail to rename attachment in config" << p_oldName << p_newName;

        m_attachments[idx].m_name = p_oldName;
        dir.rename(p_newName, p_oldName);

        return false;
    }

    return true;
}

bool VNoteFile::deleteFile(VNoteFile *p_file, QString *p_errMsg)
{
    Q_ASSERT(!p_file->isOpened());

    bool ret = true;
    QString name = p_file->getName();
    QString path = p_file->fetchPath();

    if (!p_file->deleteFile(p_errMsg)) {
        qWarning() << "fail to delete file" << name << path;
        ret = false;
    }

    VDirectory *dir = p_file->getDirectory();
    Q_ASSERT(dir);
    if (!dir->removeFile(p_file)) {
        qWarning() << "fail to remove file from directory" << name << path;
        VUtils::addErrMsg(p_errMsg, tr("Fail to remove the note from the folder configuration."));
        ret = false;
    }

    delete p_file;

    return ret;
}

bool VNoteFile::copyFile(VDirectory *p_destDir,
                         const QString &p_destName,
                         VNoteFile *p_file,
                         bool p_isCut,
                         VNoteFile **p_targetFile,
                         QString *p_errMsg)
{
    bool ret = true;
    *p_targetFile = NULL;
    int nrImageCopied = 0;
    bool attachmentFolderCopied = false;

    QString srcPath = QDir::cleanPath(p_file->fetchPath());
    QString destPath = QDir::cleanPath(QDir(p_destDir->fetchPath()).filePath(p_destName));
    if (VUtils::equalPath(srcPath, destPath)) {
        *p_targetFile = p_file;
        return false;
    }

    if (!p_destDir->isOpened()) {
        VUtils::addErrMsg(p_errMsg, tr("Fail to open target folder."));
        return false;
    }

    QString opStr = p_isCut ? tr("cut") : tr("copy");
    VDirectory *srcDir = p_file->getDirectory();
    DocType docType = p_file->getDocType();

    Q_ASSERT(srcDir->isOpened());
    Q_ASSERT(docType == VUtils::docTypeFromName(p_destName));

    // Images to be copied.
    QVector<ImageLink> images;
    if (docType == DocType::Markdown) {
        images = VUtils::fetchImagesFromMarkdownFile(p_file,
                                                     ImageLink::LocalRelativeInternal);
    }

    // Attachments to be copied.
    QString attaFolder = p_file->getAttachmentFolder();
    QString attaFolderPath;
    if (!attaFolder.isEmpty()) {
        attaFolderPath = p_file->fetchAttachmentFolderPath();
    }

    // Copy the note file.
    if (!VUtils::copyFile(srcPath, destPath, p_isCut)) {
        VUtils::addErrMsg(p_errMsg, tr("Fail to %1 the note file.").arg(opStr));
        qWarning() << "fail to" << opStr << "the note file" << srcPath << "to" << destPath;
        return false;
    }

    // Add file to VDirectory.
    VNoteFile *destFile = NULL;
    if (p_isCut) {
        srcDir->removeFile(p_file);
        p_file->setName(p_destName);
        if (p_destDir->addFile(p_file, -1)) {
            destFile = p_file;
        } else {
            destFile = NULL;
        }
    } else {
        destFile = p_destDir->addFile(p_destName, -1);
    }

    if (!destFile) {
        VUtils::addErrMsg(p_errMsg, tr("Fail to add the note to target folder's configuration."));
        return false;
    }

    // Copy images.
    QDir parentDir(destFile->fetchBasePath());
    for (int i = 0; i < images.size(); ++i) {
        const ImageLink &link = images[i];
        if (!QFileInfo::exists(link.m_path)) {
            VUtils::addErrMsg(p_errMsg, tr("Source image %1 does not exist.")
                                          .arg(link.m_path));
            ret = false;
            continue;
        }

        QString imageFolder = VUtils::directoryNameFromPath(VUtils::basePathFromPath(link.m_path));
        QString destImagePath = QDir(parentDir.filePath(imageFolder)).filePath(VUtils::fileNameFromPath(link.m_path));

        if (VUtils::equalPath(link.m_path, destImagePath)) {
            VUtils::addErrMsg(p_errMsg, tr("Skip image with the same source and target path %1.")
                                          .arg(link.m_path));
            ret = false;
            continue;
        }

        if (!VUtils::copyFile(link.m_path, destImagePath, p_isCut)) {
            VUtils::addErrMsg(p_errMsg, tr("Fail to %1 image %2 to %3. "
                                           "Please manually %1 it and modify the note.")
                                          .arg(opStr).arg(link.m_path).arg(destImagePath));
            ret = false;
        } else {
            ++nrImageCopied;
            qDebug() << opStr << "image" << link.m_path << "to" << destImagePath;
        }
    }

    // Copy attachment folder.
    if (!attaFolderPath.isEmpty()) {
        QDir dir(destFile->fetchBasePath());
        QString folderPath = dir.filePath(destFile->getNotebook()->getAttachmentFolder());
        attaFolder = VUtils::getFileNameWithSequence(folderPath, attaFolder);
        folderPath = QDir(folderPath).filePath(attaFolder);

        // Copy attaFolderPath to folderPath.
        if (!VUtils::copyDirectory(attaFolderPath, folderPath, p_isCut)) {
            VUtils::addErrMsg(p_errMsg, tr("Fail to %1 attachments folder %2 to %3. "
                                           "Please manually maintain it.")
                                          .arg(opStr).arg(attaFolderPath).arg(folderPath));
            QVector<VAttachment> emptyAttas;
            destFile->setAttachments(emptyAttas);
            ret = false;
        } else {
            attachmentFolderCopied = true;

            destFile->setAttachmentFolder(attaFolder);
            if (!p_isCut) {
                destFile->setAttachments(p_file->getAttachments());
            }
        }

        if (!p_destDir->updateFileConfig(destFile)) {
            VUtils::addErrMsg(p_errMsg, tr("Fail to update configuration of note %1.")
                                          .arg(destFile->fetchPath()));
            ret = false;
        }
    }

    qDebug() << "copyFile:" << p_file << "to" << destFile
             << "copied_images:" << nrImageCopied
             << "copied_attachments:" << attachmentFolderCopied;

    *p_targetFile = destFile;
    return ret;
}

