#ifndef VTEXTBLOCKDATA_H
#define VTEXTBLOCKDATA_H

#include <QTextBlockUserData>
#include <QVector>
#include <QDebug>

#include "vconstants.h"
#include "markdownhighlighterdata.h"
#include "vtextdocumentlayoutdata.h"

// Sources of the preview.
enum class PreviewSource
{
    ImageLink = 0,
    CodeBlock,
    MathjaxBlock,
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
                        const QSize &p_imageSize,
                        const QString &p_background)
        : m_startPos(p_startPos),
          m_endPos(p_endPos),
          m_padding(p_padding),
          m_inline(p_inline),
          m_imageName(p_imageName),
          m_imageSize(p_imageSize),
          m_background(p_background)
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
               && m_imageSize == a.m_imageSize
               && m_background == a.m_background;
    }

    bool intersect(const VPreviewedImageInfo &a) const
    {
        return !(m_endPos <= a.m_startPos || m_startPos >= a.m_endPos);
    }

    bool contains(int p_positionInBlock) const
    {
        return p_positionInBlock >= m_startPos && p_positionInBlock < m_endPos;
    }

    QString toString() const
    {
        return QString("previewed image (%1): [%2, %3) padding %4 inline %5 (%6,%7) bg(%8)")
                      .arg(m_imageName)
                      .arg(m_startPos)
                      .arg(m_endPos)
                      .arg(m_padding)
                      .arg(m_inline)
                      .arg(m_imageSize.width())
                      .arg(m_imageSize.height())
                      .arg(m_background);
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

    // Forced background before drawing this image.
    QString m_background;
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
                 const QSize &p_imageSize,
                 const QString &p_background)
        : m_source(p_source),
          m_timeStamp(p_timeStamp),
          m_imageInfo(p_startPos,
                      p_endPos,
                      p_padding,
                      p_inline,
                      p_imageName,
                      p_imageSize,
                      p_background)
    {
    }

    // Source of this preview.
    PreviewSource m_source;

    // Timestamp for this preview.
    long long m_timeStamp;

    // Image info of this preview.
    VPreviewedImageInfo m_imageInfo;
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

    TimeStamp getTimeStamp() const;

    void setTimeStamp(TimeStamp p_ts);

    TimeStamp getCodeBlockTimeStamp() const;

    void setCodeBlockTimeStamp(TimeStamp p_ts);

    bool isBlockHighlightCacheMatched(const QVector<HLUnit> &p_highlight) const;

    QVector<HLUnit> &getBlockHighlightCache();

    void setBlockHighlightCache(const QVector<HLUnit> &p_highlight);

    bool isCodeBlockHighlightCacheMatched(const QVector<HLUnitStyle> &p_highlight) const;

    QVector<HLUnitStyle> &getCodeBlockHighlightCache();

    void setCodeBlockHighlightCache(const QVector<HLUnitStyle> &p_highlight);

    bool isCacheValid() const;

    void setCacheValid(bool p_valid);

    static VTextBlockData *blockData(const QTextBlock &p_block);

    static BlockLayoutInfo *layoutInfo(const QTextBlock &p_block);

private:
    // Check the order of elements.
    bool checkOrder() const;

    // TimeStamp of the highlight result which has been applied to this block.
    TimeStamp m_timeStamp;

    // TimeStamp of the code block highlight result which has been applied to this block.
    TimeStamp m_codeBlockTimeStamp;

    // Block highlight cache.
    QVector<HLUnit> m_blockHighlightCache;

    // Code block highlight cache.
    // This cache is always valid.
    QVector<HLUnitStyle> m_codeBlockHighlightCache;

    // Whether the highlight cache is valid.
    bool m_cacheValid;

    // Sorted by m_imageInfo.m_startPos, with no two element's position intersected.
    QVector<VPreviewInfo *> m_previews;

    // Indentation of the this code block if this block is a fenced code block.
    int m_codeBlockIndentation;

    BlockLayoutInfo m_layoutInfo;
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

inline TimeStamp VTextBlockData::getTimeStamp() const
{
    return m_timeStamp;
}

inline void VTextBlockData::setTimeStamp(TimeStamp p_ts)
{
    m_timeStamp = p_ts;
}

inline TimeStamp VTextBlockData::getCodeBlockTimeStamp() const
{
    return m_codeBlockTimeStamp;
}

inline void VTextBlockData::setCodeBlockTimeStamp(TimeStamp p_ts)
{
    m_codeBlockTimeStamp = p_ts;
}

inline bool VTextBlockData::isBlockHighlightCacheMatched(const QVector<HLUnit> &p_highlight) const
{
    if (!m_cacheValid
        || p_highlight.size() != m_blockHighlightCache.size()) {
        return false;
    }

    int sz = p_highlight.size();
    for (int i = 0; i < sz; ++i)
    {
        if (!(p_highlight[i] == m_blockHighlightCache[i])) {
            return false;
        }
    }

    return true;
}

inline QVector<HLUnit> &VTextBlockData::getBlockHighlightCache()
{
    return m_blockHighlightCache;
}

inline void VTextBlockData::setBlockHighlightCache(const QVector<HLUnit> &p_highlight)
{
    m_blockHighlightCache = p_highlight;
}

inline bool VTextBlockData::isCodeBlockHighlightCacheMatched(const QVector<HLUnitStyle> &p_highlight) const
{
    if (p_highlight.size() != m_codeBlockHighlightCache.size()) {
        return false;
    }

    int sz = p_highlight.size();
    for (int i = 0; i < sz; ++i)
    {
        if (!(p_highlight[i] == m_codeBlockHighlightCache[i])) {
            return false;
        }
    }

    return true;
}

inline QVector<HLUnitStyle> &VTextBlockData::getCodeBlockHighlightCache()
{
    return m_codeBlockHighlightCache;
}

inline void VTextBlockData::setCodeBlockHighlightCache(const QVector<HLUnitStyle> &p_highlight)
{
    m_codeBlockHighlightCache = p_highlight;
}

inline bool VTextBlockData::isCacheValid() const
{
    return m_cacheValid;
}

inline void VTextBlockData::setCacheValid(bool p_valid)
{
    m_cacheValid = p_valid;
}

inline VTextBlockData *VTextBlockData::blockData(const QTextBlock &p_block)
{
    if (!p_block.isValid()) {
        return NULL;
    }

    VTextBlockData *data = static_cast<VTextBlockData *>(p_block.userData());
    if (!data) {
        data = new VTextBlockData();
        const_cast<QTextBlock &>(p_block).setUserData(data);
    }

    return data;
}

inline BlockLayoutInfo *VTextBlockData::layoutInfo(const QTextBlock &p_block)
{
    VTextBlockData *data = blockData(p_block);
    if (data) {
        return &data->m_layoutInfo;
    } else {
        return NULL;
    }
}
#endif // VTEXTBLOCKDATA_H
