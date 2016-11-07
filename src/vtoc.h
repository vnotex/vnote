#ifndef VTOC_H
#define VTOC_H

#include <QString>
#include <QVector>

enum VHeaderType
{
    Anchor = 0,
    LineNumber
};

struct VHeader
{
    int level;
    QString name;
    QString anchor;
    int lineNumber;
};

struct VAnchor
{
    VAnchor(const QString filePath, const QString &anchor, int lineNumber)
        : filePath(filePath), anchor(anchor), lineNumber(lineNumber) {}
    QString filePath;
    QString anchor;
    int lineNumber;
};

class VToc
{
public:
    VToc();

    QVector<VHeader> headers;
    int curHeaderIndex;
    int type;
    QString filePath;
    bool valid;
};

#endif // VTOC_H
