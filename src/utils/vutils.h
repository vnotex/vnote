#ifndef VUTILS_H
#define VUTILS_H

#include <QString>
#include <QColor>
#include "vconfigmanager.h"

class VUtils
{
public:
    VUtils();

    static QString readFileFromDisk(const QString &filePath);
    static bool writeFileToDisk(const QString &filePath, const QString &text);
    // Transform FFFFFF string to QRgb
    static QRgb QRgbFromString(const QString &str);
};

#endif // VUTILS_H
