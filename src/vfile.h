#ifndef VFILE_H
#define VFILE_H

#include <QObject>
#include <QString>
#include <QDir>
#include "vdirectory.h"
#include "vconstants.h"

class VFile : public QObject
{
    Q_OBJECT
public:
    explicit VFile(const QString &p_name, QObject *p_parent);
    bool open();
    void close();
    bool save();
    // Convert current file type.
    void convert(DocType p_curType, DocType p_targetType);

    inline const QString &getName() const;
    void setName(const QString &p_name);
    inline VDirectory *getDirectory();
    inline const VDirectory *getDirectory() const;
    inline DocType getDocType() const;
    inline QString &getContent();
    inline void setContent(const QString &p_content);
    inline QString retriveNotebook() const;
    inline QString retrivePath() const;
    inline QString retriveRelativePath() const;
    inline QString retriveBasePath() const;
    inline QString retriveImagePath() const;
    inline bool isModified() const;
    inline bool isOpened() const;

signals:

public slots:
    void setModified(bool p_modified);

private:
    // Delete the file and corresponding images
    void deleteDiskFile();
    // Delete local images in ./images of DocType::Markdown
    void deleteLocalImages();

    QString m_name;
    bool m_opened;
    // File has been modified in editor
    bool m_modified;
    DocType m_docType;
    QString m_content;
    friend class VDirectory;
};

inline const QString &VFile::getName() const
{
    return m_name;
}

inline VDirectory *VFile::getDirectory()
{
    Q_ASSERT(parent());
    return (VDirectory *)parent();
}

inline const VDirectory *VFile::getDirectory() const
{
    Q_ASSERT(parent());
    return (const VDirectory *)parent();
}

inline DocType VFile::getDocType() const
{
    return m_docType;
}

inline QString &VFile::getContent()
{
    return m_content;
}

inline QString VFile::retriveNotebook() const
{
    return getDirectory()->retriveNotebook();
}

inline QString VFile::retrivePath() const
{
    QString dirPath = getDirectory()->retrivePath();
    return QDir(dirPath).filePath(m_name);
}

inline QString VFile::retriveRelativePath() const
{
    QString dirRelativePath = getDirectory()->retriveRelativePath();
    return QDir(dirRelativePath).filePath(m_name);
}

inline QString VFile::retriveBasePath() const
{
    return getDirectory()->retrivePath();
}

inline QString VFile::retriveImagePath() const
{
    return QDir(retriveBasePath()).filePath("images");
}

inline void VFile::setContent(const QString &p_content)
{
    m_content = p_content;
}

inline bool VFile::isModified() const
{
    return m_modified;
}

inline bool VFile::isOpened() const
{
    return m_opened;
}

#endif // VFILE_H
