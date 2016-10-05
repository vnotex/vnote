#ifndef VCONFIGMANAGER_H
#define VCONFIGMANAGER_H

class QJsonObject;
class QString;

class VConfigManager
{
public:
    VConfigManager();

    // Read config from the directory config json file into a QJsonObject
    static QJsonObject readDirectoryConfig(const QString &path);
    static bool writeDirectoryConfig(const QString &path, const QJsonObject &configJson);
    static bool deleteDirectoryConfig(const QString &path);

private:
    // The name of the config file in each directory
    static const QString dirConfigFileName;
};

#endif // VCONFIGMANAGER_H
