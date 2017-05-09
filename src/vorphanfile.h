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
    bool open() Q_DECL_OVERRIDE;
    QString retrivePath() const Q_DECL_OVERRIDE;
    QString retriveRelativePath() const Q_DECL_OVERRIDE;
    QString retriveBasePath() const Q_DECL_OVERRIDE;
    VDirectory *getDirectory() Q_DECL_OVERRIDE;
    const VDirectory *getDirectory() const Q_DECL_OVERRIDE;
    QString getNotebookName() const Q_DECL_OVERRIDE;
    VNotebook *getNotebook() Q_DECL_OVERRIDE;

private:
    bool save() Q_DECL_OVERRIDE;
    void convert(DocType p_curType, DocType p_targetType) Q_DECL_OVERRIDE;
    void setName(const QString &p_name) Q_DECL_OVERRIDE;
    QString retriveImagePath() const Q_DECL_OVERRIDE;
    void setContent(const QString &p_content) Q_DECL_OVERRIDE;
    bool isInternalImageFolder(const QString &p_path) const Q_DECL_OVERRIDE;

    QString m_path;
    friend class VDirectory;
};

#endif // VORPHANFILE_H
