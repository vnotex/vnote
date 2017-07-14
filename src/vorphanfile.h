#ifndef VORPHANFILE_H
#define VORPHANFILE_H

#include "vfile.h"

// VOrphanFile is file not belong to any notebooks or directories.
class VOrphanFile : public VFile
{
    Q_OBJECT
public:
    VOrphanFile(const QString &p_path, QObject *p_parent, bool p_modifiable);

    bool open() Q_DECL_OVERRIDE;
    QString retrivePath() const Q_DECL_OVERRIDE;
    QString retriveRelativePath() const Q_DECL_OVERRIDE;
    QString retriveBasePath() const Q_DECL_OVERRIDE;
    VDirectory *getDirectory() Q_DECL_OVERRIDE;
    const VDirectory *getDirectory() const Q_DECL_OVERRIDE;
    QString getNotebookName() const Q_DECL_OVERRIDE;

    void setNotebookName(const QString &p_notebook);

    VNotebook *getNotebook() Q_DECL_OVERRIDE;

    // Rename file.
    bool rename(const QString &p_name) Q_DECL_OVERRIDE;

    void setImageFolder(const QString &p_path);

    // Whether the image folder is a relative path.
    bool isRelativeImageFolder() const Q_DECL_OVERRIDE;

    // Return the image folder part in an image link.
    QString getImageFolderInLink() const Q_DECL_OVERRIDE;

private:
    bool save() Q_DECL_OVERRIDE;
    void convert(DocType p_curType, DocType p_targetType) Q_DECL_OVERRIDE;
    void setName(const QString &p_name) Q_DECL_OVERRIDE;
    QString retriveImagePath() const Q_DECL_OVERRIDE;
    void setContent(const QString &p_content) Q_DECL_OVERRIDE;
    bool isInternalImageFolder(const QString &p_path) const Q_DECL_OVERRIDE;

    QString m_path;

    QString m_notebookName;

    // Image folder path of this file.
    // It could be an absolute or relative path.
    // Empty to use the global default config.
    QString m_imageFolder;

    friend class VDirectory;
};

#endif // VORPHANFILE_H
