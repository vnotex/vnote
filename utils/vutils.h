#ifndef VUTILS_H
#define VUTILS_H

#include <QString>

class VUtils
{
public:
    VUtils();

    static QString readFileFromDisk(const QString &filePath);
    static bool writeFileToDisk(const QString &filePath, const QString &text);
};

#endif // VUTILS_H
