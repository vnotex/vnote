#include "legacynotebookutils.h"

#include <QJsonDocument>
#include <QJsonArray>

#include <utils/pathutils.h>
#include <utils/fileutils.h>
#include <utils/utils.h>

using namespace vnotex;

const QString LegacyNotebookUtils::c_legacyFolderConfigFile = "_vnote.json";

static QDateTime stringToDateTime(const QString &p_str)
{
    if (p_str.isEmpty()) {
        return QDateTime::currentDateTimeUtc();
    }
    return Utils::dateTimeFromStringUniform(p_str);
}

bool LegacyNotebookUtils::isLegacyNotebookRootFolder(const QString &p_folderPath)
{
    return QFileInfo::exists(getFolderConfigFile(p_folderPath));
}

QDateTime LegacyNotebookUtils::getCreatedTimeUtcOfFolder(const QString &p_folderPath)
{
    auto ti = getFolderConfig(p_folderPath).value(QStringLiteral("created_time")).toString();
    return stringToDateTime(ti);
}

QString LegacyNotebookUtils::getAttachmentFolderOfNotebook(const QString &p_folderPath)
{
    return getFolderConfig(p_folderPath).value(QStringLiteral("attachment_folder")).toString();
}

QString LegacyNotebookUtils::getImageFolderOfNotebook(const QString &p_folderPath)
{
    return getFolderConfig(p_folderPath).value(QStringLiteral("image_folder")).toString();
}

QString LegacyNotebookUtils::getRecycleBinFolderOfNotebook(const QString &p_folderPath)
{
    return getFolderConfig(p_folderPath).value(QStringLiteral("recycle_bin_folder")).toString();
}

QString LegacyNotebookUtils::getFolderConfigFile(const QString &p_folderPath)
{
    return PathUtils::concatenateFilePath(p_folderPath, c_legacyFolderConfigFile);
}

QJsonObject LegacyNotebookUtils::getFolderConfig(const QString &p_folderPath)
{
    auto configFile = getFolderConfigFile(p_folderPath);
    return QJsonDocument::fromJson(FileUtils::readFile(configFile)).object();
}

void LegacyNotebookUtils::removeFolderConfigFile(const QString &p_folderPath)
{
    auto configFile = getFolderConfigFile(p_folderPath);
    FileUtils::removeFile(configFile);
}

void LegacyNotebookUtils::forEachFolder(const QJsonObject &p_config, std::function<void(const QString &p_name)> p_func)
{
    auto folderArray = p_config.value(QStringLiteral("sub_directories")).toArray();
    for (int i = 0; i < folderArray.size(); ++i) {
        const auto name = folderArray[i].toObject().value(QStringLiteral("name")).toString();
        p_func(name);
    }
}

void LegacyNotebookUtils::forEachFile(const QJsonObject &p_config, std::function<void(const LegacyNotebookUtils::FileInfo &p_info)> p_func)
{
    auto fileArray = p_config.value(QStringLiteral("files")).toArray();
    for (int i = 0; i < fileArray.size(); ++i) {
        const auto obj = fileArray[i].toObject();
        FileInfo info;
        info.m_name = obj.value(QStringLiteral("name")).toString();
        {
            auto ti = obj.value(QStringLiteral("created_time")).toString();
            info.m_createdTimeUtc = stringToDateTime(ti);
        }
        {
            auto ti = obj.value(QStringLiteral("created_time")).toString();
            info.m_modifiedTimeUtc = stringToDateTime(ti);
        }
        info.m_attachmentFolder = obj.value(QStringLiteral("attachment_folder")).toString();
        {
            auto arr = obj.value(QStringLiteral("tags")).toArray();
            for (int i = 0; i < arr.size(); ++i) {
                info.m_tags << arr[i].toString();
            }
        }
        p_func(info);
    }
}
