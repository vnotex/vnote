#ifndef VNOTEBOOK_H
#define VNOTEBOOK_H

#include <QObject>
#include <QString>
#include <QDateTime>

class VDirectory;
class VFile;
class VNoteFile;

class VNotebook : public QObject
{
    Q_OBJECT
public:
    VNotebook(const QString &name, const QString &path, QObject *parent = 0);

    ~VNotebook();

    // Open the root directory to load contents
    bool open();

    // Whether this notebook is opened.
    bool isOpened() const;

    // Close all the directory and files of this notebook.
    // Please make sure all files belonging to this notebook have been closed in the tab.
    void close();

    bool containsFile(const VFile *p_file) const;

    // Try to load the file @p_path.
    // Returns the corresponding VNoteFile struct if @p_path is a note inside this notebook.
    // Otherwise, returns NULL.
    // If notebook is not opened currently, it will open itself and close itself
    // if @p_path is not inside this notebook.
    VNoteFile *tryLoadFile(const QString &p_path);

    // Try to load the directory @p_path.
    // Returns the corresponding VDirectory struct if @p_path is a folder inside this notebook.
    // Otherwise, returns NULL.
    // If notebook is not opened currently, it will open itself and close itself
    // if @p_path is not inside this notebook.
    VDirectory *tryLoadDirectory(const QString &p_path);

    const QString &getName() const;

    const QString &getPath() const;

    const QString &getPathInConfig() const;

    void updatePath(const QString &p_path);

    VDirectory *getRootDir() const;

    void rename(const QString &p_name);

    static VNotebook *createNotebook(const QString &p_name,
                                     const QString &p_path,
                                     bool p_import,
                                     const QString &p_imageFolder,
                                     const QString &p_attachmentFolder,
                                     QObject *p_parent = 0);

    static bool deleteNotebook(VNotebook *p_notebook, bool p_deleteFiles);

    // Get the image folder for this notebook to use (not exactly the same as
    // m_imageFolder if it is empty).
    const QString &getImageFolder() const;

    // Return m_imageFolder.
    const QString &getImageFolderConfig() const;

    // Different from image folder. We could not change the attachment folder
    // of a notebook once it has been created.
    // Get the attachment folder for this notebook to use.
    const QString &getAttachmentFolder() const;

    // Return m_recycleBinFolder.
    const QString &getRecycleBinFolder() const;

    // Get the recycle folder path for this notebook to use.
    QString getRecycleBinFolderPath() const;

    void setImageFolder(const QString &p_imageFolder);

    void setAttachmentFolder(const QString &p_attachmentFolder);

    // Read configurations (only notebook part) directly from root directory config file.
    bool readConfigNotebook();

    // Write configurations only related to notebook to root directory config file.
    bool writeConfigNotebook() const;

    // Return only the info of notebook part in json.
    QJsonObject toConfigJsonNotebook() const;

    // Need to check if this notebook has been opened.
    QDateTime getCreatedTimeUtc();

    bool isValid() const;

    QList<QString> collectFiles();

private:
    // Serialize current instance to json.
    QJsonObject toConfigJson() const;

    // Write current instance to config file.
    bool writeToConfig() const;

    void setPath(const QString &p_path);

    QString m_name;

    QString m_path;

    // Path in vnote.ini.
    // May be relative path to VNote's executable.
    QString m_pathInConfig;

    // Folder name to store images.
    // If not empty, VNote will store images in this folder within the same directory of the note.
    // Otherwise, VNote will use the global configured folder.
    QString m_imageFolder;

    // Folder name to store attachments.
    // Should not be empty and changed once a notebook is created.
    QString m_attachmentFolder;

    // Folder name to store deleted files.
    // Could be relative or absolute.
    QString m_recycleBinFolder;

    // Parent is NULL for root directory
    VDirectory *m_rootDir;

    // Whether this notebook is valid.
    // Will set to true after readConfigNotebook().
    bool m_valid;
};

inline VDirectory *VNotebook::getRootDir() const
{
    return m_rootDir;
}

inline const QString &VNotebook::getRecycleBinFolder() const
{
    return m_recycleBinFolder;
}

inline bool VNotebook::isValid() const
{
    return m_valid;
}

inline const QString &VNotebook::getPath() const
{
    return m_path;
}

inline const QString &VNotebook::getPathInConfig() const
{
    return m_pathInConfig;
}

inline const QString &VNotebook::getName() const
{
    return m_name;
}

#endif // VNOTEBOOK_H
