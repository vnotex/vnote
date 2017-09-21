#ifndef VDIRECTORY_H
#define VDIRECTORY_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QPointer>
#include <QJsonObject>
#include <QDateTime>
#include "vnotebook.h"

class VFile;
class VNoteFile;

class VDirectory : public QObject
{
    Q_OBJECT
public:
    VDirectory(VNotebook *p_notebook,
               const QString &p_name,
               QObject *p_parent = 0,
               QDateTime p_createdTimeUtc = QDateTime());

    bool open();
    void close();
    VDirectory *createSubDirectory(const QString &p_name);

    // Returns the VDirectory with the name @p_name directly in this directory.
    VDirectory *findSubDirectory(const QString &p_name, bool p_caseSensitive);

    // Returns the VNoteFile with the name @p_name directly in this directory.
    VNoteFile *findFile(const QString &p_name, bool p_caseSensitive);

    // If current dir or its sub-dir contains @p_file.
    bool containsFile(const VFile *p_file) const;

    VNoteFile *createFile(const QString &p_name);

    // Remove and delete subdirectory @p_subDir.
    void deleteSubDirectory(VDirectory *p_subDir, bool p_skipRecycleBin = false);

    // Remove the file in the config and m_files without deleting it in the disk.
    // It won't change the parent of @p_file to enable it find its path.
    bool removeFile(VNoteFile *p_file);

    // Remove the directory in the config and m_subDirs without deleting it in the disk.
    // It won't change the parent of @p_dir to enable it find its path.
    bool removeSubDirectory(VDirectory *p_dir);

    // Add the file in the config and m_files. If @p_index is -1, add it at the end.
    // @p_name: the file name of the file to add.
    // Return the VNoteFile if succeed.
    VNoteFile *addFile(const QString &p_name, int p_index);

    // Delete @p_file both from disk and config, as well as its local images.
    void deleteFile(VNoteFile *p_file);

    // Rename current directory to @p_name.
    bool rename(const QString &p_name);

    // Copy @p_srcFile to @p_destDir, setting new name to @p_destName.
    // @p_cut: copy or cut.
    // Returns the dest VNoteFile.
    static VNoteFile *copyFile(VDirectory *p_destDir, const QString &p_destName,
                               VNoteFile *p_srcFile, bool p_cut);

    static VDirectory *copyDirectory(VDirectory *p_destDir, const QString &p_destName,
                                     VDirectory *p_srcDir, bool p_cut);

    const QVector<VDirectory *> &getSubDirs() const;
    const QString &getName() const;
    void setName(const QString &p_name);
    bool isOpened() const;
    VDirectory *getParentDirectory();
    const VDirectory *getParentDirectory() const;
    VNotebook *getNotebook();
    const VNotebook *getNotebook() const;
    const QVector<VNoteFile *> &getFiles() const;
    QString fetchPath() const;
    QString fetchBasePath() const;
    QString fetchRelativePath() const;
    QString getNotebookName() const;
    bool isExpanded() const;
    void setExpanded(bool p_expanded);

    // Reorder files in m_files by index.
    // Move [@p_first, @p_last] to @p_destStart.
    void reorderFiles(int p_first, int p_last, int p_destStart);

    // Serialize current instance to json.
    // Not including sections belonging to notebook.
    QJsonObject toConfigJson() const;

    // Read configurations (excluding "sub_directories" and "files" section)
    // from config file.
    bool readConfig();

    // Write current instance to config file.
    // If it is root directory, this will include sections belonging to
    // notebook.
    bool writeToConfig() const;

    // Write the config of @p_file to config file.
    bool updateFileConfig(const VNoteFile *p_file);

    // Try to load file given relative path @p_filePath.
    VNoteFile *tryLoadFile(QStringList &p_filePath);

    QDateTime getCreatedTimeUtc() const;

private:
    // Get the path of @p_dir recursively
    QString fetchPath(const VDirectory *p_dir) const;
    // Get teh relative path of @p_dir recursively related to the notebook path
    QString fetchRelativePath(const VDirectory *p_dir) const;

    // Write @p_json to config.
    bool writeToConfig(const QJsonObject &p_json) const;

    // Add notebook part config to @p_json.
    // Should only be called with root directory.
    void addNotebookConfig(QJsonObject &p_json) const;

    // Add the file in the config and m_files. If @p_index is -1, add it at the end.
    bool addFile(VNoteFile *p_file, int p_index);

    // Add the directory in the config and m_subDirs. If @p_index is -1, add it at the end.
    // Return the VDirectory if succeed.
    VDirectory *addSubDirectory(const QString &p_name, int p_index);

    // Add the directory in the config and m_subDirs. If @p_index is -1, add it at the end.
    bool addSubDirectory(VDirectory *p_dir, int p_index);

    // Notebook containing this folder.
    QPointer<VNotebook> m_notebook;

    // Name of this folder.
    QString m_name;

    // Owner of the sub-directories
    QVector<VDirectory *> m_subDirs;

    // Owner of the files
    QVector<VNoteFile *> m_files;

    // Whether the directory has been opened.
    bool m_opened;

    // Whether expanded in the directory tree.
    bool m_expanded;

    // UTC time when creating this directory.
    // Loaded after open().
    QDateTime m_createdTimeUtc;
};

inline const QVector<VDirectory *> &VDirectory::getSubDirs() const
{
    return m_subDirs;
}

inline const QString &VDirectory::getName() const
{
    return m_name;
}

inline void VDirectory::setName(const QString &p_name)
{
    m_name = p_name;
}

inline bool VDirectory::isOpened() const
{
    return m_opened;
}

inline VDirectory *VDirectory::getParentDirectory()
{
    return (VDirectory *)this->parent();
}

inline const VDirectory *VDirectory::getParentDirectory() const
{
    return (const VDirectory *)this->parent();
}

inline const QVector<VNoteFile *> &VDirectory::getFiles() const
{
    return m_files;
}

inline QString VDirectory::getNotebookName() const
{
    return m_notebook->getName();
}

inline VNotebook *VDirectory::getNotebook()
{
    return m_notebook;
}

inline const VNotebook *VDirectory::getNotebook() const
{
    return m_notebook;
}

inline QString VDirectory::fetchPath() const
{
    return fetchPath(this);
}

inline QString VDirectory::fetchRelativePath() const
{
    return fetchRelativePath(this);
}

inline bool VDirectory::isExpanded() const
{
    return m_expanded;
}

inline QDateTime VDirectory::getCreatedTimeUtc() const
{
    return m_createdTimeUtc;
}

#endif // VDIRECTORY_H
