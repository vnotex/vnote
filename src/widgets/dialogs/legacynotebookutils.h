#ifndef LEGACYNOTEBOOKUTILS_H
#define LEGACYNOTEBOOKUTILS_H

#include <QStringList>
#include <QDateTime>
#include <QJsonObject>

#include <functional>

namespace vnotex
{
    class LegacyNotebookUtils
    {
    public:
        struct FileInfo
        {
            QString m_name;

            QDateTime m_createdTimeUtc = QDateTime::currentDateTimeUtc();

            QDateTime m_modifiedTimeUtc = QDateTime::currentDateTimeUtc();

            QString m_attachmentFolder;

            QStringList m_tags;
        };

        LegacyNotebookUtils() = delete;

        static bool isLegacyNotebookRootFolder(const QString &p_folderPath);

        static QDateTime getCreatedTimeUtcOfFolder(const QString &p_folderPath);

        static QString getAttachmentFolderOfNotebook(const QString &p_folderPath);

        static QString getImageFolderOfNotebook(const QString &p_folderPath);

        static QString getRecycleBinFolderOfNotebook(const QString &p_folderPath);

        static QJsonObject getFolderConfig(const QString &p_folderPath);

        static void removeFolderConfigFile(const QString &p_folderPath);

        static void forEachFolder(const QJsonObject &p_config, std::function<void(const QString &p_name)> p_func);

        static void forEachFile(const QJsonObject &p_config, std::function<void(const LegacyNotebookUtils::FileInfo &p_info)> p_func);

    private:
        static QString getFolderConfigFile(const QString &p_folderPath);

        static const QString c_legacyFolderConfigFile;
    };
}

#endif // LEGACYNOTEBOOKUTILS_H
