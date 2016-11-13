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
private:
    static inline void addQssVarToMap(QVector<QPair<QString, QString> > &map,
                                      const QString &key, const QString &value);
};

inline void VUtils::addQssVarToMap(QVector<QPair<QString, QString> > &map,
                                   const QString &key, const QString &value)
{
    map.append(QPair<QString, QString>(key, value));
}

#endif // VUTILS_H
