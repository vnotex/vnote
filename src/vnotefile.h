#ifndef VNOTEFILE_H
#define VNOTEFILE_H

#include "vfile.h"

class VDirectory;
class VNotebook;

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

    // Create a VNoteFile from @p_json Json object.
    static VNoteFile *fromJson(VDirectory *p_directory,
                               const QJsonObject &p_json,
                               FileType p_type,
                               bool p_modifiable);

    // Create a Json object from current instance.
    QJsonObject toConfigJson() const;

    // Delete this file in disk as well as all its images/attachments.
    bool deleteFile();

private:
    // Delete internal images of this file.
    void deleteInternalImages();
};

#endif // VNOTEFILE_H
