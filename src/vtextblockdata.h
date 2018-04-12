#ifndef VTEXTBLOCKDATA_H
#define VTEXTBLOCKDATA_H

#include <QTextBlockUserData>
#include <QVector>

// Sources of the preview.
enum class PreviewSource
{
    ImageLink = 0,
    CodeBlock,
    MaxNumberOfSources
};


// Info about a previewed image.
struct VPreviewedImageInfo
{
    VPreviewedImageInfo()
        : m_startPos(-1),
          m_endPos(-1),
          m_padding(0),
          m_inline(false)
    {
    }

    VPreviewedImageInfo(int p_startPos,
                        int p_endPos,
                        int p_padding,
                        bool p_inline,
                        const QString &p_imageName,
                        const QSize &p_imageSize)
        : m_startPos(p_startPos),
          m_endPos(p_endPos),
          m_padding(p_padding),
          m_inline(p_inline),
          m_imageName(p_imageName),
          m_imageSize(p_imageSize)
    {
    }

    bool operator<(const VPreviewedImageInfo &a) const
    {
        return m_endPos <= a.m_startPos;
    }

    bool operator==(const VPreviewedImageInfo &a) const
    {
        return m_startPos == a.m_startPos
               && m_endPos == a.m_endPos
               && m_padding == a.m_padding
               && m_inline == a.m_inline
               && m_imageName == a.m_imageName
               && m_imageSize == a.m_imageSize;
    }

    bool intersect(const VPreviewedImageInfo &a) const
    {
        return !(m_endPos <= a.m_startPos || m_startPos >= a.m_endPos);
    }

    QString toString() const
    {
        return QString("previewed image (%1): [%2, %3] padding %4 inline %5 (%6,%7)")
                      .arg(m_imageName)
                      .arg(m_startPos)
                      .arg(m_endPos)
                      .arg(m_padding)
                      .arg(m_inline)
                      .arg(m_imageSize.width())
                      .arg(m_imageSize.height());
    }

    // Start position of text corresponding to the image within block.
    int m_startPos;

    // End position of text corresponding to the image within block.
    int m_endPos;

    // Padding of the image. Only valid for block image.
    int m_padding;

    // Whether it is inline image or block image.
    bool m_inline;

    // Image name in the resource manager.
    QString m_imageName;

    // Image size of the image. Cache for performance.
    QSize m_imageSize;
};


struct VPreviewInfo
{
    VPreviewInfo()
        : m_source(PreviewSource::ImageLink),
          m_timeStamp(0)
    {
    }

    VPreviewInfo(PreviewSource p_source,
                 long long p_timeStamp,
                 int p_startPos,
                 int p_endPos,
                 int p_padding,
                 bool p_inline,
                 const QString &p_imageName,
                 const QSize &p_imageSize)
        : m_source(p_source),
          m_timeStamp(p_timeStamp),
          m_imageInfo(p_startPos,
                      p_endPos,
                      p_padding,
                      p_inline,
                      p_imageName,
                      p_imageSize)
    {
    }

    // Source of this preview.
    PreviewSource m_source;

    // Timestamp for this preview.
    long long m_timeStamp;

    // Image info of this preview.
    VPreviewedImageInfo m_imageInfo;
};


struct MathjaxInfo
{
public:
    MathjaxInfo()
        : m_isBlock(false),
          m_index(-1),
          m_length(0)
    {
    }


    bool isValid() const
    {
        return m_index >= 0 && m_length > 0;
    }

    bool isBlock() const
    {
        return m_isBlock;
    }

    void clear()
    {
        m_isBlock = false;
        m_index = -1;
        m_length = 0;
    }

    // Inline or block formula.
    bool m_isBlock;

    // Start index wihtin block, including the start mark.
    int m_index;

    // Length of this mathjax, including the end mark.
    int m_length;
};


// User data for each block.
class VTextBlockData : public QTextBlockUserData
{
public:
    VTextBlockData();

    ~VTextBlockData();

    // Insert @p_info into m_previews, preserving the order.
    // Returns true if only timestamp is updated.
    bool insertPreviewInfo(VPreviewInfo *p_info);

    // For degub only.
    QString toString() const;

    const QVector<VPreviewInfo *> &getPreviews() const;

    // Return true if there have obsolete preview being deleted.
    bool clearObsoletePreview(long long p_timeStamp, PreviewSource p_source);

    int getCodeBlockIndentation() const;

    void setCodeBlockIndentation(int p_indent);

    void clearMathjax();

    const MathjaxInfo &getPendingMathjax() const;

    void setPendingMathjax(const MathjaxInfo &p_info);

    const QVector<MathjaxInfo> getMathjax() const;

    void addMathjax(const MathjaxInfo &p_info);

private:
    // Check the order of elements.
    bool checkOrder() const;

    // Sorted by m_imageInfo.m_startPos, with no two element's position intersected.
    QVector<VPreviewInfo *> m_previews;

    // Indentation of the this code block if this block is a fenced code block.
    int m_codeBlockIndentation;

    // Pending Mathjax info, such as this block is the start of a Mathjax formula.
    MathjaxInfo m_pendingMathjax;

    // Mathjax info ends in this block.
    QVector<MathjaxInfo> m_mathjax;
};

inline const QVector<VPreviewInfo *> &VTextBlockData::getPreviews() const
{
    return m_previews;
}

inline int VTextBlockData::getCodeBlockIndentation() const
{
    return m_codeBlockIndentation;
}

inline void VTextBlockData::setCodeBlockIndentation(int p_indent)
{
    m_codeBlockIndentation = p_indent;
}

inline void VTextBlockData::clearMathjax()
{
    m_pendingMathjax.clear();
    m_mathjax.clear();
}

inline const MathjaxInfo &VTextBlockData::getPendingMathjax() const
{
    return m_pendingMathjax;
}

inline void VTextBlockData::setPendingMathjax(const MathjaxInfo &p_info)
{
    m_pendingMathjax = p_info;
}

inline const QVector<MathjaxInfo> VTextBlockData::getMathjax() const
{
    return m_mathjax;
}

inline void VTextBlockData::addMathjax(const MathjaxInfo &p_info)
{
    m_mathjax.append(p_info);
}

#endif // VTEXTBLOCKDATA_H
