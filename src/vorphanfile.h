#ifndef VORPHANFILE_H
#define VORPHANFILE_H

#include "vfile.h"

// VOrphanFile is file not belong to any notebooks or directories.
// Given the path of the file, VNote could load its content as read-only.
class VOrphanFile : public VFile
{
    Q_OBJECT
public:
    VOrphanFile(const QString &p_path, QObject *p_parent);
    bool open();
    QString retrivePath() const;
    QString retriveRelativePath() const;
    QString retriveBasePath() const;
    VDirectory *getDirectory();
    const VDirectory *getDirectory() const;
    QString getNotebookName() const;
    VNotebook *getNotebook();

private:
    bool save();
    void convert(DocType p_curType, DocType p_targetType);
    void setName(const QString &p_name);
    QString retriveImagePath() const;
    void setContent(const QString &p_content);

    QString m_path;
    friend class VDirectory;
};

#endif // VORPHANFILE_H
