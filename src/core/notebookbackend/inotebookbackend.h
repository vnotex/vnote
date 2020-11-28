#ifndef INOTEBOOKBACKEND_H
#define INOTEBOOKBACKEND_H

#include <QObject>

#include <utils/pathutils.h>

class QByteArray;
class QJsonObject;

namespace vnotex
{
    // Abstract class for notebook backend, which is responsible for file access
    // and synchronization.
    class INotebookBackend : public QObject
    {
        Q_OBJECT
    public:
        INotebookBackend(const QString &p_rootPath, QObject *p_parent = nullptr)
            : QObject(p_parent),
              m_rootPath(PathUtils::absolutePath(p_rootPath))
        {
        }

        virtual ~INotebookBackend()
        {
        }

        virtual QString getName() const = 0;

        virtual QString getDisplayName() const = 0;

        virtual QString getDescription() const = 0;

        const QString &getRootPath() const
        {
            return m_rootPath;
        }

        void setRootPath(const QString &p_rootPath)
        {
            m_rootPath = p_rootPath;
        }

        // Whether @p_dirPath is an empty directory.
        virtual bool isEmptyDir(const QString &p_dirPath) const = 0;

        // Create the directory path @p_dirPath. Create all parent directories if necessary.
        virtual void makePath(const QString &p_dirPath) = 0;

        // Write @p_data to @p_filePath.
        virtual void writeFile(const QString &p_filePath, const QByteArray &p_data) = 0;

        // Write @p_text to @p_filePath.
        virtual void writeFile(const QString &p_filePath, const QString &p_text) = 0;

        // Write @p_jobj to @p_filePath.
        virtual void writeFile(const QString &p_filePath, const QJsonObject &p_jobj) = 0;

        // Read content from @p_filePath.
        virtual QString readTextFile(const QString &p_filePath) = 0;

        // Read file @p_filePath.
        virtual QByteArray readFile(const QString &p_filePath) = 0;

        QString getFullPath(const QString &p_path) const;

        virtual bool exists(const QString &p_path) const = 0;

        virtual bool childExistsCaseInsensitive(const QString &p_dirPath, const QString &p_name) const = 0;

        virtual bool isFile(const QString &p_path) const = 0;

        virtual void renameFile(const QString &p_filePath, const QString &p_name) = 0;

        virtual void renameDir(const QString &p_dirPath, const QString &p_name) = 0;

        // Copy @p_filePath to @p_destPath.
        // @p_filePath could be outside notebook.
        virtual void copyFile(const QString &p_filePath, const QString &p_destPath) = 0;

        // Delete @p_filePath from disk.
        virtual void removeFile(const QString &p_filePath) = 0;

        // Copy  @p_dirPath to as @p_destPath.
        virtual void copyDir(const QString &p_dirPath, const QString &p_destPath) = 0;

        // Delete @p_dirPath from disk if it is empty.
        // Return false if it is not deleted due to non-empty.
        virtual bool removeDirIfEmpty(const QString &p_dirPath) = 0;

        virtual void removeDir(const QString &p_dirPath) = 0;

        virtual QString renameIfExistsCaseInsensitive(const QString &p_path) const = 0;

        // Add one file to backend.
        virtual void addFile(const QString &p_path) = 0;

        virtual void removeEmptyDir(const QString &p_dirPath) = 0;

    protected:
        // Constrain @p_path within root path of the notebook.
        void constrainPath(const QString &p_path) const;

    private:
        // Root path of the notebook.
        QString m_rootPath;
    };
} // ns vnotex

#endif // INOTEBOOKBACKEND_H
