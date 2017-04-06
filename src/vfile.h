#ifndef VFILE_H
#define VFILE_H

#include <QObject>
#include <QString>
#include "vdirectory.h"
#include "vconstants.h"

class VNotebook;

class VFile : public QObject
{
    Q_OBJECT
public:
    VFile(const QString &p_name, QObject *p_parent,
          FileType p_type = FileType::Normal, bool p_modifiable = true);
    virtual ~VFile();
    virtual bool open();
    virtual void close();
    virtual bool save();
    // Convert current file type.
    virtual void convert(DocType p_curType, DocType p_targetType);

    const QString &getName() const;
    virtual void setName(const QString &p_name);
    virtual VDirectory *getDirectory();
    virtual const VDirectory *getDirectory() const;
    DocType getDocType() const;
    const QString &getContent() const;
    virtual void setContent(const QString &p_content);
    virtual VNotebook *getNotebook();
    virtual QString getNotebookName() const;
    virtual QString retrivePath() const;
    virtual QString retriveRelativePath() const;
    virtual QString retriveBasePath() const;
    virtual QString retriveImagePath() const;
    bool isModified() const;
    bool isModifiable() const;
    bool isOpened() const;
    FileType getType() const;

public slots:
    void setModified(bool p_modified);

protected:
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
    FileType m_type;
    bool m_modifiable;

    friend class VDirectory;
};

#endif // VFILE_H
