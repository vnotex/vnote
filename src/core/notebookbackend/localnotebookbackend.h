#ifndef LOCALNOTEBOOKBACKEND_H
#define LOCALNOTEBOOKBACKEND_H

#include "inotebookbackend.h"

#include "../global.h"

namespace vnotex
{
    // Backend to access local file system.
    class LocalNotebookBackend : public INotebookBackend
    {
        Q_OBJECT
    public:
        explicit LocalNotebookBackend(const QString &p_name,
                                      const QString &p_displayName,
                                      const QString &p_description,
                                      const QString &p_rootPath,
                                      QObject *p_parent = nullptr);

        QString getName() const Q_DECL_OVERRIDE;

        QString getDisplayName() const Q_DECL_OVERRIDE;

        QString getDescription() const Q_DECL_OVERRIDE;

        // Whether @p_dirPath is an empty directory.
        bool isEmptyDir(const QString &p_dirPath) const Q_DECL_OVERRIDE;

        // Create the directory path @p_dirPath. Create all parent directories if necessary.
        void makePath(const QString &p_dirPath) Q_DECL_OVERRIDE;

        // Write @p_data to @p_filePath.
        void writeFile(const QString &p_filePath, const QByteArray &p_data) Q_DECL_OVERRIDE;

        // Write @p_text to @p_filePath.
        void writeFile(const QString &p_filePath, const QString &p_text) Q_DECL_OVERRIDE;

        // Write @p_jobj to @p_filePath.
        void writeFile(const QString &p_filePath, const QJsonObject &p_jobj) Q_DECL_OVERRIDE;

        // Read content from @p_filePath.
        QString readTextFile(const QString &p_filePath) Q_DECL_OVERRIDE;

        // Read file @p_filePath.
        QByteArray readFile(const QString &p_filePath) Q_DECL_OVERRIDE;

        bool exists(const QString &p_path) const Q_DECL_OVERRIDE;

        bool childExistsCaseInsensitive(const QString &p_dirPath, const QString &p_name) const Q_DECL_OVERRIDE;

        bool isFile(const QString &p_path) const Q_DECL_OVERRIDE;

        void renameFile(const QString &p_filePath, const QString &p_name) Q_DECL_OVERRIDE;

        void renameDir(const QString &p_dirPath, const QString &p_name) Q_DECL_OVERRIDE;

        // Delete @p_filePath from disk.
        void removeFile(const QString &p_filePath) Q_DECL_OVERRIDE;

        // Delete @p_dirPath from disk if it is empty.
        bool removeDirIfEmpty(const QString &p_dirPath) Q_DECL_OVERRIDE;

        void removeDir(const QString &p_dirPath) Q_DECL_OVERRIDE;

        // Copy @p_filePath to @p_destPath.
        // @p_filePath may beyond this notebook backend.
        void copyFile(const QString &p_filePath, const QString &p_destPath) Q_DECL_OVERRIDE;

        // Copy @p_dirPath to as @p_destPath.
        void copyDir(const QString &p_dirPath, const QString &p_destPath) Q_DECL_OVERRIDE;

        QString renameIfExistsCaseInsensitive(const QString &p_path) const Q_DECL_OVERRIDE;

        void addFile(const QString &p_path) Q_DECL_OVERRIDE;

        void removeEmptyDir(const QString &p_dirPath) Q_DECL_OVERRIDE;

    private:
        Info m_info;
    };
} // ns vnotex

#endif // LOCALNOTEBOOKBACKEND_H
