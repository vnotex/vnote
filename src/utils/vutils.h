#ifndef VUTILS_H
#define VUTILS_H

#include <QString>
#include <QColor>
#include <QVector>
#include <QPair>
#include "vconfigmanager.h"

class VUtils
{
public:
    VUtils();

    static QString readFileFromDisk(const QString &filePath);
    static bool writeFileToDisk(const QString &filePath, const QString &text);
    // Transform FFFFFF string to QRgb
    static QRgb QRgbFromString(const QString &str);
    static QString generateImageFileName(const QString &path, const QString &title,
                                         const QString &format = "png");
    static void processStyle(QString &style);
    static bool isMarkdown(const QString &fileName);
    static inline QString directoryNameFromPath(const QString& path);
    static QString fileNameFromPath(const QString &path);
    static QString basePathFromPath(const QString &path);
    static QVector<QString> imagesFromMarkdownFile(const QString &filePath);
private:
    static inline void addQssVarToMap(QVector<QPair<QString, QString> > &map,
                                      const QString &key, const QString &value);
};

inline void VUtils::addQssVarToMap(QVector<QPair<QString, QString> > &map,
                                   const QString &key, const QString &value)
{
    map.append(QPair<QString, QString>(key, value));
}

inline QString VUtils::directoryNameFromPath(const QString &path)
{
    return fileNameFromPath(path);
}

#endif // VUTILS_H
