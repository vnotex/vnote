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
    // Init m_opened, m_modified, and m_content.
    virtual bool open();

    // Close the file.
    // Clear m_modified, m_content, m_opened.
    virtual void close();

    // Save m_content to the file.
    virtual bool save();

    const QString &getName() const;

    DocType getDocType() const;

    bool isModified() const;

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

public slots:
    void setModified(bool p_modified);

protected:
    // Name of this file.
    QString m_name;

    // Whether this file has been opened (content loaded).
    bool m_opened;

    // m_content is different from that in the disk.
    bool m_modified;

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
};

inline const QString &VFile::getName() const
{
    return m_name;
}

inline DocType VFile::getDocType() const
{
    return m_docType;
}

inline bool VFile::isModified() const
{
    return m_modified;
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
    Q_ASSERT(m_opened);
    return m_content;
}

inline void VFile::setContent(const QString &p_content)
{
    m_content = p_content;
    m_modified = true;
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
