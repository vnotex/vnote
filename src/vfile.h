#ifndef VFILE_H
#define VFILE_H

#include <QObject>
#include <QString>
#include <QUrl>
#include <QDateTime>
#include "vconstants.h"

// VFile is an abstract class representing a file in VNote.
class VFile : public QObject
{
    Q_OBJECT
public:
    VFile(QObject *p_parent,
          const QString &p_name,
          FileType p_type,
          bool p_modifiable,
          QDateTime p_createdTimeUtc,
          QDateTime p_modifiedTimeUtc);

    virtual ~VFile();

    // Open the file to load content into m_content.
    // Init m_opened, and m_content.
    virtual bool open();

    // Close the file.
    // Clear m_content, m_opened.
    virtual void close();

    // Save m_content to the file.
    virtual bool save();

    // Reload content from disk.
    virtual bool reload();

    const QString &getName() const;

    DocType getDocType() const;

    bool isModifiable() const;

    bool isOpened() const;

    FileType getType() const;

    const QString &getContent() const;

    void setContent(const QString &p_content);

    // Get the absolute full path of the file.
    virtual QString fetchPath() const = 0;

    // Get the absolute full path of the directory containing the file.
    virtual QString fetchBasePath() const = 0;

    // The path of the image folder to store images of this file.
    virtual QString fetchImageFolderPath() const = 0;

    // Return the base URL for this file when loaded in VWebView.
    QUrl getBaseUrl() const;

    // Whether the directory @p_path is an internal image folder of this file.
    // It is true only when the folder is in the same directory as the parent
    // directory of this file or equals to fetchImageFolderPath().
    bool isInternalImageFolder(const QString &p_path) const;

    // Whether use a relative image folder.
    virtual bool useRelativeImageFolder() const = 0;

    // Return the image folder part in an image link.
    virtual QString getImageFolderInLink() const = 0;

    QDateTime getCreatedTimeUtc() const;

    QDateTime getModifiedTimeUtc() const;

    // Whether this file was changed outside VNote.
    bool isChangedOutside(bool &p_missing) const;

    // Return backup file of previous session if there exists one.
    QString backupFileOfPreviousSession() const;

    // Write @p_content to backup file.
    bool writeBackupFile(const QString &p_content);

    QString readBackupFile(const QString &p_file);

protected:
    // Name of this file.
    QString m_name;

    // Whether this file has been opened (content loaded).
    bool m_opened;

    // DocType of this file: Html, Markdown.
    DocType m_docType;

    // Content of this file.
    QString m_content;

    // FileType of this file: Note, Orphan.
    FileType m_type;

    // Whether this file is modifiable.
    bool m_modifiable;

    // UTC time when creating this file.
    QDateTime m_createdTimeUtc;

    // UTC time of last modification to this file in VNote.
    QDateTime m_modifiedTimeUtc;

    // Last modified date and local time when the file is last modified
    // corresponding to m_content.
    QDateTime m_lastModified;

    // Name of the backup file.
    QString m_backupName;

    // Used to identify file path change.
    QString m_lastBackupFilePath;

private:
    // Fetch backup file path.
    QString fetchBackupFilePath();

    QStringList getPotentialBackupFiles(const QString &p_dir) const;

    // Read the file content to check if it is a backup file.
    bool isBackupFile(const QString &p_file) const;

    QString fetchBackupFileHead() const;

    static const QString c_backupFileHeadMagic;
};

inline const QString &VFile::getName() const
{
    return m_name;
}

inline DocType VFile::getDocType() const
{
    return m_docType;
}

inline bool VFile::isModifiable() const
{
    return m_modifiable;
}

inline bool VFile::isOpened() const
{
    return m_opened;
}

inline FileType VFile::getType() const
{
    return m_type;
}

inline const QString &VFile::getContent() const
{
    return m_content;
}

inline void VFile::setContent(const QString &p_content)
{
    m_content = p_content;
}

inline QDateTime VFile::getCreatedTimeUtc() const
{
    return m_createdTimeUtc;
}

inline QDateTime VFile::getModifiedTimeUtc() const
{
    return m_modifiedTimeUtc;
}

#endif // VFILE_H
