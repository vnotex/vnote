#ifndef VNOTEBOOK_H
#define VNOTEBOOK_H

#include <QObject>
#include <QString>

class VDirectory;

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

    QString getName() const;
    QString getPath() const;
    void setName(const QString &name);
    void setPath(const QString &path);
    inline VDirectory *getRootDir();

signals:
    void contentChanged();

private:
    QString m_name;
    QString m_path;
    // Parent is NULL for root directory
    VDirectory *m_rootDir;
};

inline VDirectory *VNotebook::getRootDir()
{
    return m_rootDir;
}

#endif // VNOTEBOOK_H
