#ifndef VTOC_H
#define VTOC_H

#include <QString>
#include <QVector>

class VFile;

enum VHeaderType
{
    Anchor = 0,
    LineNumber
};

struct VHeader
{
    VHeader() : level(1), lineNumber(-1), index(-1) {}
    VHeader(int level, const QString &name, const QString &anchor, int lineNumber, int index)
        : level(level), name(name), anchor(anchor), lineNumber(lineNumber), index(index) {}
    int level;
    QString name;
    QString anchor;
    int lineNumber;

    // Index in the outline, based on 0.
    int index;

    // Whether it is an empty (fake) header.
    bool isEmpty() const
    {
        if (anchor.isEmpty()) {
            return lineNumber == -1;
        } else {
            return anchor == "#";
        }
    }
};

struct VAnchor
{
    VAnchor() : m_file(NULL), lineNumber(-1), m_outlineIndex(-1) {}

    VAnchor(const VFile *file, const QString &anchor, int lineNumber, int outlineIndex = -1)
        : m_file(file), anchor(anchor), lineNumber(lineNumber), m_outlineIndex(outlineIndex) {}

    // The file this anchor points to.
    const VFile *m_file;

    // The string anchor. For Web view.
    QString anchor;

    // The line number anchor. For edit view.
    int lineNumber;

    // Index of the header for this anchor in VToc outline.
    // Used to translate current header between read and edit mode.
    int m_outlineIndex;

    bool operator==(const VAnchor &p_anchor) const {
        return (p_anchor.m_file == m_file
                && p_anchor.anchor == anchor
                && p_anchor.lineNumber == lineNumber
                && p_anchor.m_outlineIndex == m_outlineIndex);
    }
};

class VToc
{
public:
    VToc();

    QVector<VHeader> headers;
    int type;
    const VFile *m_file;
    bool valid;
};

#endif // VTOC_H
