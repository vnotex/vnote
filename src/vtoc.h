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
    VHeader() : level(1), lineNumber(-1) {}
    VHeader(int level, const QString &name, const QString &anchor, int lineNumber)
        : level(level), name(name), anchor(anchor), lineNumber(lineNumber) {}
    int level;
    QString name;
    QString anchor;
    int lineNumber;
};

struct VAnchor
{
    VAnchor() : lineNumber(-1), m_outlineIndex(0) {}
    VAnchor(const QString filePath, const QString &anchor, int lineNumber)
        : filePath(filePath), anchor(anchor), lineNumber(lineNumber), m_outlineIndex(0) {}
    QString filePath;
    QString anchor;
    int lineNumber;
    // Index of this anchor in VToc outline.
    int m_outlineIndex;

    bool operator==(const VAnchor &p_anchor) const {
        return (p_anchor.filePath == filePath
                && p_anchor.anchor == anchor
                && p_anchor.lineNumber == lineNumber);
    }
};

class VToc
{
public:
    VToc();

    QVector<VHeader> headers;
    int type;
    QString filePath;
    bool valid;
};

#endif // VTOC_H
