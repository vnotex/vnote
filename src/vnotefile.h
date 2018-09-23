#ifndef VNOTEFILE_H
#define VNOTEFILE_H

#include <QVector>
#include <QString>

#include "vfile.h"
#include "utils/vutils.h"

class VDirectory;
class VNotebook;

// Structure for a note attachment.
struct VAttachment
{
    VAttachment()
    {
    }

    VAttachment(const QString &p_name)
        : m_name(p_name)
    {
    }

    // File name of the attachment.
    QString m_name;
};

class VNoteFile : public VFile
{
    Q_OBJECT
public:
    VNoteFile(VDirectory *p_directory,
              const QString &p_name,
              FileType p_type,
              bool p_modifiable,
              QDateTime p_createdTimeUtc,
              QDateTime p_modifiedTimeUtc);

    QString fetchPath() const Q_DECL_OVERRIDE;

    QString fetchBasePath() const Q_DECL_OVERRIDE;

    QString fetchImageFolderPath() const Q_DECL_OVERRIDE;

    bool useRelativeImageFolder() const Q_DECL_OVERRIDE;

    QString getImageFolderInLink() const Q_DECL_OVERRIDE;

    bool save() Q_DECL_OVERRIDE;

    // Set the name of this file.
    void setName(const QString &p_name);

    // Rename the name of this file in disk and config.
    bool rename(const QString &p_name);

    VDirectory *getDirectory();

    const VDirectory *getDirectory() const;

    VNotebook *getNotebook();

    const VNotebook *getNotebook() const;

    QString getNotebookName() const;

    // Get the relative path related to the notebook.
    QString fetchRelativePath() const;

    // Create a Json object from current instance.
    QJsonObject toConfigJson() const;

    const QString &getAttachmentFolder() const;

    void setAttachmentFolder(const QString &p_folder);

    const QVector<VAttachment> &getAttachments() const;

    void setAttachments(const QVector<VAttachment> &p_attas);

    // Add @p_file as an attachment to this note.
    bool addAttachment(const QString &p_file);

    // Fetch attachment folder path.
    // Will create it if it does not exist.
    QString fetchAttachmentFolderPath();

    // Delete all the attachments.
    // @p_omitMissing: omit the error if the attachment file does not exist.
    bool deleteAttachments(bool p_omitMissing = false);

    // Delete attachments specified by @p_names.
    // @p_omitMissing: omit the error if the attachment file does not exist.
    bool deleteAttachments(const QVector<QString> &p_names,
                           bool p_omitMissing = false);

    // Reorder attachments in m_attachments by index.
    bool sortAttachments(const QVector<int> &p_sortedIdx);

    // Return the index of @p_name in m_attachments.
    // -1 if not found.
    int findAttachment(const QString &p_name, bool p_caseSensitive = true);

    // Rename attachment @p_oldName to @p_newName.
    bool renameAttachment(const QString &p_oldName, const QString &p_newName);

    // Check if all the attachment files still exist.
    // Return the missing attachments' names.
    QVector<QString> checkAttachments();

    const QStringList &getTags() const;

    void removeTag(const QString &p_tag);

    bool addTag(const QString &p_tag);

    bool hasTag(const QString &p_tag) const;

    // Create a VNoteFile from @p_json Json object.
    static VNoteFile *fromJson(VDirectory *p_directory,
                               const QJsonObject &p_json,
                               FileType p_type,
                               bool p_modifiable);

    // Delete file @p_file including removing it from parent directory configuration
    // and delete the file in disk.
    // @p_file: should be a normal file with parent directory.
    // @p_errMsg: if not NULL, it will contain error message if this function fails.
    static bool deleteFile(VNoteFile *p_file, QString *p_errMsg = NULL);

    // Copy file @p_file to @p_destDir with new name @p_destName.
    // Returns a file representing the destination file after copy/cut.
    static bool copyFile(VDirectory *p_destDir,
                         const QString &p_destName,
                         VNoteFile *p_file,
                         bool p_isCut,
                         int p_idx,
                         VNoteFile **p_targetFile,
                         QString *p_errMsg = NULL);

    // Copy images @p_images of a file to @p_destDirPath.
    static bool copyInternalImages(const QVector<ImageLink> &p_images,
                                   const QString &p_destDirPath,
                                   bool p_isCut,
                                   int *p_nrImageCopied,
                                   QString *p_errMsg = NULL);

private:
    // Delete internal images of this file.
    // Return true only when all internal images were deleted successfully.
    bool deleteInternalImages();

    // Delete this file in disk as well as all its images/attachments.
    bool deleteFile(QString *p_msg = NULL);

    // Folder under the attachment folder of the notebook.
    // Store all the attachments of current file.
    QString m_attachmentFolder;

    // Attachments.
    QVector<VAttachment> m_attachments;

    // Tags of this file.
    QStringList m_tags;
};

inline const QString &VNoteFile::getAttachmentFolder() const
{
    return m_attachmentFolder;
}

inline void VNoteFile::setAttachmentFolder(const QString &p_folder)
{
    m_attachmentFolder = p_folder;
}

inline const QVector<VAttachment> &VNoteFile::getAttachments() const
{
    return m_attachments;
}

inline void VNoteFile::setAttachments(const QVector<VAttachment> &p_attas)
{
    m_attachments = p_attas;
}

inline const QStringList &VNoteFile::getTags() const
{
    return m_tags;
}

inline bool VNoteFile::hasTag(const QString &p_tag) const
{
    return m_tags.contains(p_tag);
}
#endif // VNOTEFILE_H
