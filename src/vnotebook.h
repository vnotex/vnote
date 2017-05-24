#ifndef VNOTEBOOK_H
#define VNOTEBOOK_H

#include <QObject>
#include <QString>

class VDirectory;
class VFile;

class VNotebook : public QObject
{
    Q_OBJECT
public:
    VNotebook(const QString &name, const QString &path, QObject *parent = 0);
    ~VNotebook();

    // Open the root directory to load contents
    bool open();

    // Close all the directory and files of this notebook.
    // Please make sure all files belonging to this notebook have been closed in the tab.
    void close();

    bool containsFile(const VFile *p_file) const;

    QString getName() const;
    QString getPath() const;
    inline VDirectory *getRootDir();
    void rename(const QString &p_name);

    static VNotebook *createNotebook(const QString &p_name, const QString &p_path,
                                     bool p_import, const QString &p_imageFolder,
                                     QObject *p_parent = 0);

    static bool deleteNotebook(VNotebook *p_notebook, bool p_deleteFiles);

    // Get the image folder for this notebook to use (not exactly the same as
    // m_imageFolder if it is empty).
    const QString &getImageFolder() const;

    // Return m_imageFolder.
    const QString &getImageFolderConfig() const;

    void setImageFolder(const QString &p_imageFolder);

    // Read configurations (excluding "sub_directories" and "files" section)
    // from root directory config file.
    bool readConfig();

    // Write configurations (excluding "sub_directories" and "files" section)
    // to root directory config file.
    bool writeConfig() const;

signals:
    void contentChanged();

private:
    // Serialize current instance to json.
    QJsonObject toConfigJson() const;

    // Write current instance to config file.
    bool writeToConfig() const;

    QString m_name;
    QString m_path;

    // Folder name to store images.
    // If not empty, VNote will store images in this folder within the same directory of the note.
    // Otherwise, VNote will use the global configured folder.
    QString m_imageFolder;

    // Parent is NULL for root directory
    VDirectory *m_rootDir;
};

inline VDirectory *VNotebook::getRootDir()
{
    return m_rootDir;
}

#endif // VNOTEBOOK_H
