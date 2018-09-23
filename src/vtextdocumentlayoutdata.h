#ifndef VTEXTDOCUMENTLAYOUTDATA_H
#define VTEXTDOCUMENTLAYOUTDATA_H

#include "utils/vutils.h"

// Denote the start and end position of a marker line.
struct Marker
{
    QPointF m_start;
    QPointF m_end;
};

struct ImagePaintInfo
{
    // The rect to draw the image.
    QRectF m_rect;

    // Name of the image.
    QString m_name;

    // Forced background.
    QColor m_background;

    bool isValid() const
    {
        return !m_name.isEmpty();
    }

    bool hasForcedBackground() const
    {
        return m_background.isValid();
    }
};

struct BlockLayoutInfo
{
    BlockLayoutInfo()
        : m_offset(-1)
    {
    }

    void reset()
    {
        m_offset = -1;
        m_rect = QRectF();
        m_markers.clear();
        m_images.clear();
    }

    bool isNull() const
    {
        return m_rect.isNull();
    }

    bool hasOffset() const
    {
        return m_offset > -1 && !m_rect.isNull();
    }

    qreal top() const
    {
        V_ASSERT(hasOffset());
        return m_offset;
    }

    qreal bottom() const
    {
        V_ASSERT(hasOffset());
        return m_offset + m_rect.height();
    }

    // Y offset of this block.
    // -1 for invalid.
    qreal m_offset;

    // The bounding rect of this block, including the margins.
    // Null for invalid.
    QRectF m_rect;

    // Markers to draw for this block.
    // Y is the offset within this block.
    QVector<Marker> m_markers;

    // Images to draw for this block.
    // Y is the offset within this block.
    QVector<ImagePaintInfo> m_images;
};
#endif // VTEXTDOCUMENTLAYOUTDATA_H
