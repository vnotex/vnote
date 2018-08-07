#ifndef VTEXTDOCUMENTLAYOUT_H
#define VTEXTDOCUMENTLAYOUT_H

#include <QAbstractTextDocumentLayout>
#include <QVector>
#include <QSize>
#include <QMap>

#include "vconstants.h"
#include "vtextdocumentlayoutdata.h"

class VImageResourceManager2;
struct VPreviewedImageInfo;
struct VPreviewInfo;

struct QMapDummyValue
{
};

typedef QMap<int, QMapDummyValue> OrderedIntSet;

class VTextDocumentLayout : public QAbstractTextDocumentLayout
{
    Q_OBJECT
public:
    VTextDocumentLayout(QTextDocument *p_doc,
                        VImageResourceManager2 *p_imageMgr);

    void draw(QPainter *p_painter, const PaintContext &p_context) Q_DECL_OVERRIDE;

    int hitTest(const QPointF &p_point, Qt::HitTestAccuracy p_accuracy) const Q_DECL_OVERRIDE;

    int pageCount() const Q_DECL_OVERRIDE;

    QSizeF documentSize() const Q_DECL_OVERRIDE;

    QRectF frameBoundingRect(QTextFrame *p_frame) const Q_DECL_OVERRIDE;

    QRectF blockBoundingRect(const QTextBlock &p_block) const Q_DECL_OVERRIDE;

    void setCursorWidth(int p_width);

    int cursorWidth() const;

    void setLineLeading(qreal p_leading);

    qreal getLineLeading() const;

    // Return the block number which contains point @p_point.
    // If @p_point is at the border, returns the block below.
    int findBlockByPosition(const QPointF &p_point) const;

    void setImageWidthConstrainted(bool p_enabled);

    void setBlockImageEnabled(bool p_enabled);

    // Relayout all the blocks.
    void relayout();

    // Relayout @p_blocks.
    void relayout(const OrderedIntSet &p_blocks);

    void setImageLineColor(const QColor &p_color);

    void setCursorBlockMode(CursorBlock p_mode);

    void setVirtualCursorBlockWidth(int p_width);

    void clearLastCursorBlockWidth();

    void setHighlightCursorLineBlockEnabled(bool p_enabled);

    void setCursorLineBlockBg(const QColor &p_bg);

    void setCursorLineBlockNumber(int p_blockNumber);

    // Request update block by block number.
    void updateBlockByNumber(int p_blockNumber);

signals:
    // Emit to update current cursor block width if m_cursorBlockMode is enabled.
    void cursorBlockWidthUpdated(int p_width);

protected:
    void documentChanged(int p_from, int p_charsRemoved, int p_charsAdded) Q_DECL_OVERRIDE;

private:
    // Layout one block.
    // Only update the rect of the block. Offset is not updated yet.
    void layoutBlock(const QTextBlock &p_block);

    // Update the offset of @p_block.
    // @p_block has a valid layout.
    // After the call, all block before @p_block will have the correct layout and offset.
    void updateOffsetBefore(const QTextBlock &p_block);

    // Update the offset of blocks after @p_block if they are layouted.
    void updateOffsetAfter(const QTextBlock &p_block);

    void updateOffset(const QTextBlock &p_block);

    void layoutBlockAndUpdateOffset(const QTextBlock &p_block);

    // Returns the total height of this block after layouting lines and inline
    // images.
    qreal layoutLines(const QTextBlock &p_block,
                      QTextLayout *p_tl,
                      QVector<Marker> &p_markers,
                      QVector<ImagePaintInfo> &p_images,
                      qreal p_availableWidth,
                      qreal p_height);

    // Layout inline image in a line.
    // @p_info: if NULL, means just layout a marker.
    // Returns the image height.
    void layoutInlineImage(const VPreviewedImageInfo *p_info,
                           qreal p_heightInBlock,
                           qreal p_imageSpaceHeight,
                           qreal p_xStart,
                           qreal p_xEnd,
                           QVector<Marker> &p_markers,
                           QVector<ImagePaintInfo> &p_images);

    // Get inline images belonging to @p_line from @p_info.
    // @p_index: image [0, p_index) has been drawn.
    // @p_images: contains all images and markers (NULL element indicates it
    // is just a placeholder for the marker.
    // Returns the maximum height of the images.
    qreal fetchInlineImagesForOneLine(const QVector<VPreviewInfo *> &p_info,
                                      const QTextLine *p_line,
                                      qreal p_margin,
                                      int &p_index,
                                      QVector<const VPreviewedImageInfo *> &p_images,
                                      QVector<QPair<qreal, qreal>> &p_imageRange);

    // Clear the layout of @p_block.
    // Also clear all the offset behind this block.
    void clearBlockLayout(QTextBlock &p_block);

    // Update rect of a block.
    void finishBlockLayout(const QTextBlock &p_block,
                           const QVector<Marker> &p_markers,
                           const QVector<ImagePaintInfo> &p_images);

    void updateDocumentSize();

    QVector<QTextLayout::FormatRange> formatRangeFromSelection(const QTextBlock &p_block,
                                                               const QVector<Selection> &p_selections) const;

    // Get the block range [first, last] by rect @p_rect.
    // @p_rect: a clip region in document coordinates. If null, returns all the blocks.
    // Return [-1, -1] if no valid block range found.
    void blockRangeFromRect(const QRectF &p_rect, int &p_first, int &p_last) const;

    // Binary search to get the block range [first, last] by @p_rect.
    void blockRangeFromRectBS(const QRectF &p_rect, int &p_first, int &p_last) const;

    // Return a rect from the layout.
    // If @p_imageRect is not NULL and there is block image for this block, it will
    // be set to the rect of that image.
    // Return a null rect if @p_block has not been layouted.
    QRectF blockRectFromTextLayout(const QTextBlock &p_block,
                                   ImagePaintInfo *p_image = NULL);

    // Update document size when only @p_block is changed and the height
    // remains the same.
    void updateDocumentSizeWithOneBlockChanged(const QTextBlock &p_block);

    void adjustImagePaddingAndSize(const VPreviewedImageInfo *p_info,
                                   int p_maximumWidth,
                                   int &p_padding,
                                   QSize &p_size) const;

    // Draw images of block @p_block.
    // @p_offset: the offset for the drawing of the block.
    void drawImages(QPainter *p_painter,
                    const QTextBlock &p_block,
                    const QPointF &p_offset);

    void drawMarkers(QPainter *p_painter,
                     const QTextBlock &p_block,
                     const QPointF &p_offset);

    void scaleSize(QSize &p_size, int p_width, int p_height);

    // Get text length in pixel.
    // @p_pos: position within the layout.
    int getTextWidthWithinTextLine(const QTextLayout *p_layout, int p_pos, int p_length);

    // Document margin on left/right/bottom.
    qreal m_margin;

    // Maximum width of the contents.
    qreal m_width;

    // The block number of the block which contains the m_width.
    int m_maximumWidthBlockNumber;

    // Height of all the document (all the blocks).
    qreal m_height;

    // Set the leading space of a line.
    qreal m_lineLeading;

    // Block count of the document.
    int m_blockCount;

    // Width of the cursor.
    int m_cursorWidth;

    // Right margin for cursor.
    qreal m_cursorMargin;

    VImageResourceManager2 *m_imageMgr;

    bool m_blockImageEnabled;

    // Whether constraint the width of image to the width of the page.
    bool m_imageWidthConstrainted;

    // Color of the image line.
    QColor m_imageLineColor;

    // Draw cursor as block.
    CursorBlock m_cursorBlockMode;

    // Virtual cursor block: cursor block on no character.
    int m_virtualCursorBlockWidth;

    int m_lastCursorBlockWidth;

    // Whether highlight the block containing the cursor line.
    bool m_highlightCursorLineBlock;

    // The cursor line's block background color.
    QColor m_cursorLineBlockBg;

    // The block containing the cursor.
    int m_cursorLineBlockNumber;
};

inline qreal VTextDocumentLayout::getLineLeading() const
{
    return m_lineLeading;
}

inline void VTextDocumentLayout::setImageLineColor(const QColor &p_color)
{
    m_imageLineColor = p_color;
}

inline void VTextDocumentLayout::scaleSize(QSize &p_size, int p_width, int p_height)
{
    if (p_size.width() > p_width || p_size.height() > p_height) {
        p_size.scale(p_width, p_height, Qt::KeepAspectRatio);
    }
}

inline void VTextDocumentLayout::setCursorBlockMode(CursorBlock p_mode)
{
    m_cursorBlockMode = p_mode;
}

inline void VTextDocumentLayout::setVirtualCursorBlockWidth(int p_width)
{
    m_virtualCursorBlockWidth = p_width;
}

inline void VTextDocumentLayout::clearLastCursorBlockWidth()
{
    m_lastCursorBlockWidth = -1;
}

inline void VTextDocumentLayout::setHighlightCursorLineBlockEnabled(bool p_enabled)
{
    if (m_highlightCursorLineBlock != p_enabled) {
        m_highlightCursorLineBlock = p_enabled;
        if (!m_highlightCursorLineBlock) {
            int pre = m_cursorLineBlockNumber;
            m_cursorLineBlockNumber = -1;
            updateBlockByNumber(pre);
        }
    }
}

inline void VTextDocumentLayout::setCursorLineBlockBg(const QColor &p_bg)
{
    if (p_bg != m_cursorLineBlockBg) {
        m_cursorLineBlockBg = p_bg;
        updateBlockByNumber(m_cursorLineBlockNumber);
    }
}

inline void VTextDocumentLayout::setCursorLineBlockNumber(int p_blockNumber)
{
    if (p_blockNumber != m_cursorLineBlockNumber) {
        int pre = m_cursorLineBlockNumber;
        m_cursorLineBlockNumber = p_blockNumber;

        if (m_highlightCursorLineBlock) {
            updateBlockByNumber(pre);
            updateBlockByNumber(m_cursorLineBlockNumber);
        }
    }
}

inline void VTextDocumentLayout::setCursorWidth(int p_width)
{
    m_cursorWidth = p_width;
}

inline int VTextDocumentLayout::cursorWidth() const
{
    return m_cursorWidth;
}

inline void VTextDocumentLayout::layoutBlockAndUpdateOffset(const QTextBlock &p_block)
{
    layoutBlock(p_block);
    updateOffset(p_block);
}

inline void VTextDocumentLayout::updateOffset(const QTextBlock &p_block)
{
    updateOffsetBefore(p_block);
    updateOffsetAfter(p_block);
}
#endif // VTEXTDOCUMENTLAYOUT_H
