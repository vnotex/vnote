#ifndef VORPHANFILE_H
#define VORPHANFILE_H

#include "vfile.h"

// VOrphanFile is a file not belonging to any notebooks or directories.
// Such as external files, system files.
// It uses the file path to locate and identify a file.
class VOrphanFile : public VFile
{
    Q_OBJECT
public:
    VOrphanFile(QObject *p_parent,
                const QString &p_path,
                bool p_modifiable,
                bool p_systemFile = false);

    QString fetchPath() const Q_DECL_OVERRIDE;

    QString fetchBasePath() const Q_DECL_OVERRIDE;

    QString fetchImageFolderPath() const Q_DECL_OVERRIDE;

    // Whether use a relative image folder.
    bool useRelativeImageFolder() const Q_DECL_OVERRIDE;

    // Return the image folder part in an image link.
    QString getImageFolderInLink() const Q_DECL_OVERRIDE;

    // Return image folder config.
    const QString getImageFolder() const;

    // Set the image folder config.
    void setImageFolder(const QString &p_path);

    bool isSystemFile() const;

private:
    // Full path of this file.
    QString m_path;

    // Image folder path of this file.
    // It could be an absolute or relative path.
    // Empty to use the global default config.
    // Valid only within a session.
    QString m_imageFolder;

    // Whether it is a system internal file.
    bool m_systemFile;
};

inline const QString VOrphanFile::getImageFolder() const
{
    return m_imageFolder;
}

inline void VOrphanFile::setImageFolder(const QString &p_path)
{
    m_imageFolder = p_path;
}

inline bool VOrphanFile::isSystemFile() const
{
    return m_systemFile;
}

#endif // VORPHANFILE_H
