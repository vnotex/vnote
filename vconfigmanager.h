#ifndef VCONFIGMANAGER_H
#define VCONFIGMANAGER_H

#include <QFont>
#include <QPalette>
#include <QVector>

#include "hgmarkdownhighlighter.h"

class QJsonObject;
class QString;

#define VConfigInst VConfigManager::getInst()

class VConfigManager
{
public:
    static VConfigManager *getInst();
    // Read config from the directory config json file into a QJsonObject
    static QJsonObject readDirectoryConfig(const QString &path);
    static bool writeDirectoryConfig(const QString &path, const QJsonObject &configJson);
    static bool deleteDirectoryConfig(const QString &path);

    void updateMarkdownEditStyle();

    QFont baseEditFont;
    QPalette baseEditPalette;
    QPalette mdEditPalette;
    QVector<HighlightingStyle> mdHighlightingStyles;

private:
    VConfigManager();
    void initialize();
    // The name of the config file in each directory
    static const QString dirConfigFileName;
    static VConfigManager *instance;
};

#endif // VCONFIGMANAGER_H
