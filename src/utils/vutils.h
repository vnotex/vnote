#ifndef VUTILS_H
#define VUTILS_H

#include <QString>
#include <QColor>
#include <QVector>
#include <QPair>
#include "vconfigmanager.h"
#include "vconstants.h"

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
    static void processStyle(QString &style, const QVector<QPair<QString, QString> > &varMap);
    static bool isMarkdown(const QString &fileName);
    static inline QString directoryNameFromPath(const QString& path);
    static QString fileNameFromPath(const QString &path);
    static QString basePathFromPath(const QString &path);
    static QVector<QString> imagesFromMarkdownFile(const QString &filePath);
    static void makeDirectory(const QString &path);
    static ClipboardOpType opTypeInClipboard();
    static bool copyFile(const QString &p_srcFilePath, const QString &p_destFilePath, bool p_isCut);
};

inline QString VUtils::directoryNameFromPath(const QString &path)
{
    return fileNameFromPath(path);
}

#endif // VUTILS_H
