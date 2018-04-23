#include "vtextdocumentlayout.h"

#include <QTextDocument>
#include <QTextBlock>
#include <QTextFrame>
#include <QTextLayout>
#include <QPointF>
#include <QFontMetrics>
#include <QFont>
#include <QPainter>
#include <QDebug>

#include "vimageresourcemanager2.h"
#include "vtextedit.h"
#include "vtextblockdata.h"

#define MARKER_THICKNESS        2
#define MAX_INLINE_IMAGE_HEIGHT 400

VTextDocumentLayout::VTextDocumentLayout(QTextDocument *p_doc,
                                         VImageResourceManager2 *p_imageMgr)
    : QAbstractTextDocumentLayout(p_doc),
      m_margin(p_doc->documentMargin()),
      m_width(0),
      m_maximumWidthBlockNumber(-1),
      m_height(0),
      m_lineLeading(0),
      m_blockCount(0),
      m_cursorWidth(1),
      m_cursorMargin(4),
      m_imageMgr(p_imageMgr),
      m_blockImageEnabled(false),
      m_imageWidthConstrainted(false),
      m_imageLineColor("#9575CD"),
      m_cursorBlockMode(CursorBlock::None),
      m_virtualCursorBlockWidth(8),
      m_lastCursorBlockWidth(-1),
      m_highlightCursorLineBlock(false),
      m_cursorLineBlockBg("#C0C0C0"),
      m_cursorLineBlockNumber(-1)
{
}

static void fillBackground(QPainter *p_painter,
                           const QRectF &p_rect,
                           QBrush p_brush,
                           QRectF p_gradientRect = QRectF())
{
    p_painter->save();
    if (p_brush.style() >= Qt::LinearGradientPattern
        && p_brush.style() <= Qt::ConicalGradientPattern) {
        if (!p_gradientRect.isNull()) {
            QTransform m = QTransform::fromTranslate(p_gradientRect.left(),
                                                     p_gradientRect.top());
            m.scale(p_gradientRect.width(), p_gradientRect.height());
            p_brush.setTransform(m);
            const_cast<QGradient *>(p_brush.gradient())->setCoordinateMode(QGradient::LogicalMode);
        }
    } else {
        p_painter->setBrushOrigin(p_rect.topLeft());
    }

    p_painter->fillRect(p_rect, p_brush);
    p_painter->restore();
}

void VTextDocumentLayout::blockRangeFromRect(const QRectF &p_rect,
                                             int &p_first,
                                             int &p_last) const
{
    if (p_rect.isNull()) {
        p_first = 0;
        p_last = m_blocks.size() - 1;
        return;
    }

    p_first = -1;
    p_last = m_blocks.size() - 1;
    int y = p_rect.y();
    Q_ASSERT(document()->blockCount() == m_blocks.size());
    QTextBlock block = document()->firstBlock();
    while (block.isValid()) {
        const BlockInfo &info = m_blocks[block.blockNumber()];
        Q_ASSERT(info.hasOffset());

        if (info.top() == y
            || (info.top() < y && info.bottom() >= y)) {
            p_first = block.blockNumber();
            break;
        }

        block = block.next();
    }

    if (p_first == -1) {
        p_last = -1;
        return;
    }

    y += p_rect.height();
    while (block.isValid()) {
        const BlockInfo &info = m_blocks[block.blockNumber()];
        Q_ASSERT(info.hasOffset());

        if (info.bottom() > y) {
            p_last = block.blockNumber();
            break;
        }

        block = block.next();
    }
}

void VTextDocumentLayout::blockRangeFromRectBS(const QRectF &p_rect,
                                               int &p_first,
                                               int &p_last) const
{
    if (p_rect.isNull()) {
        p_first = 0;
        p_last = m_blocks.size() - 1;
        return;
    }

    Q_ASSERT(document()->blockCount() == m_blocks.size());

    p_first = findBlockByPosition(p_rect.topLeft());

    if (p_first == -1) {
        p_last = -1;
        return;
    }

    int y = p_rect.bottom();
    QTextBlock block = document()->findBlockByNumber(p_first);

    if (m_blocks[p_first].top() == p_rect.top()
        && p_first > 0) {
        --p_first;
    }

    p_last = m_blocks.size() - 1;
    while (block.isValid()) {
        const BlockInfo &info = m_blocks[block.blockNumber()];
        Q_ASSERT(info.hasOffset());

        if (info.bottom() > y) {
            p_last = block.blockNumber();
            break;
        }

        block = block.next();
    }
}

int VTextDocumentLayout::findBlockByPosition(const QPointF &p_point) const
{
    int first = 0, last = m_blocks.size() - 1;
    int y = p_point.y();
    while (first <= last) {
        int mid = (first + last) / 2;
        const BlockInfo &info = m_blocks[mid];
        Q_ASSERT(info.hasOffset());
        if (info.top() <= y && info.bottom() > y) {
            // Found it.
            return mid;
        } else if (info.top() > y) {
            last = mid - 1;
        } else {
            first = mid + 1;
        }
    }

    int idx = previousValidBlockNumber(m_blocks.size());
    if (y >= m_blocks[idx].bottom()) {
        return idx;
    }

    idx = nextValidBlockNumber(-1);
    if (y < m_blocks[idx].top()) {
        return idx;
    }

    Q_ASSERT(false);
    return -1;
}

void VTextDocumentLayout::draw(QPainter *p_painter, const PaintContext &p_context)
{
    // Find out the blocks.
    int first, last;
    blockRangeFromRectBS(p_context.clip, first, last);
    if (first == -1) {
        return;
    }

    QTextDocument *doc = document();
    Q_ASSERT(doc->blockCount() == m_blocks.size());
    QPointF offset(m_margin, m_blocks[first].top());
    QTextBlock block = doc->findBlockByNumber(first);
    QTextBlock lastBlock = doc->findBlockByNumber(last);

    QPen oldPen = p_painter->pen();
    p_painter->setPen(p_context.palette.color(QPalette::Text));

    while (block.isValid()) {
        const BlockInfo &info = m_blocks[block.blockNumber()];
        Q_ASSERT(info.hasOffset());

        const QRectF &rect = info.m_rect;
        QTextLayout *layout = block.layout();

        if (!block.isVisible()) {
            offset.ry() += rect.height();
            if (block == lastBlock) {
                break;
            }

            block = block.next();
            continue;
        }

        QTextBlockFormat blockFormat = block.blockFormat();
        QBrush bg = blockFormat.background();
        if (bg != Qt::NoBrush) {
            int x = offset.x();
            int y = offset.y();
            fillBackground(p_painter,
                           rect.adjusted(x, y, x, y),
                           bg);
        }

        auto selections = formatRangeFromSelection(block, p_context.selections);

        // Draw the cursor.
        int blpos = block.position();
        int bllen = block.length();
        bool drawCursor = p_context.cursorPosition >= blpos
                          && p_context.cursorPosition < blpos + bllen;
        int cursorWidth = m_cursorWidth;
        int cursorPosition = p_context.cursorPosition - blpos;
        if (drawCursor && m_cursorBlockMode != CursorBlock::None) {
            if (cursorPosition > 0 && m_cursorBlockMode == CursorBlock::LeftSide) {
                --cursorPosition;
            }

            if (cursorPosition == bllen - 1) {
                cursorWidth = m_virtualCursorBlockWidth;
            } else {
                // Get the width of the selection to update cursor width.
                cursorWidth = getTextWidthWithinTextLine(layout, cursorPosition, 1);
                if (cursorWidth < m_cursorWidth) {
                    cursorWidth = m_cursorWidth;
                }
            }

            if (cursorWidth != m_lastCursorBlockWidth) {
                m_lastCursorBlockWidth = cursorWidth;
                emit cursorBlockWidthUpdated(m_lastCursorBlockWidth);
            }
        }

        // Draw cursor line block.
        if (m_highlightCursorLineBlock
            && m_cursorLineBlockNumber == block.blockNumber()) {
            int x = offset.x();
            int y = offset.y();
            fillBackground(p_painter,
                           rect.adjusted(x, y, x, y),
                           m_cursorLineBlockBg);
        }

        layout->draw(p_painter,
                     offset,
                     selections,
                     p_context.clip.isValid() ? p_context.clip : QRectF());

        drawImages(p_painter, block, offset);

        drawMarkers(p_painter, block, offset);

        if (drawCursor
            || (p_context.cursorPosition < -1
                && !layout->preeditAreaText().isEmpty())) {
            if (p_context.cursorPosition < -1) {
                cursorPosition = layout->preeditAreaPosition()
                                 - (p_context.cursorPosition + 2);
            }

            layout->drawCursor(p_painter,
                               offset,
                               cursorPosition,
                               cursorWidth);
        }

        offset.ry() += rect.height();
        if (block == lastBlock) {
            break;
        }

        block = block.next();
    }

    p_painter->setPen(oldPen);
}

QVector<QTextLayout::FormatRange> VTextDocumentLayout::formatRangeFromSelection(const QTextBlock &p_block,
                                                                                const QVector<Selection> &p_selections) const
{
    QVector<QTextLayout::FormatRange> ret;

    int blpos = p_block.position();
    int bllen = p_block.length();
    for (int i = 0; i < p_selections.size(); ++i) {
        const QAbstractTextDocumentLayout::Selection &range = p_selections.at(i);
        const int selStart = range.cursor.selectionStart() - blpos;
        const int selEnd = range.cursor.selectionEnd() - blpos;
        if (selStart < bllen
            && selEnd > 0
            && selEnd > selStart) {
            QTextLayout::FormatRange o;
            o.start = selStart;
            o.length = selEnd - selStart;
            o.format = range.format;
            ret.append(o);
        } else if (!range.cursor.hasSelection()
                   && range.format.hasProperty(QTextFormat::FullWidthSelection)
                   && p_block.contains(range.cursor.position())) {
            // For full width selections we don't require an actual selection, just
            // a position to specify the line. that's more convenience in usage.
            QTextLayout::FormatRange o;
            QTextLine l = p_block.layout()->lineForTextPosition(range.cursor.position() - blpos);
            Q_ASSERT(l.isValid());
            o.start = l.textStart();
            o.length = l.textLength();
            if (o.start + o.length == bllen - 1) {
                ++o.length; // include newline
            }

            o.format = range.format;
            ret.append(o);
        }
    }

    return ret;
}

int VTextDocumentLayout::hitTest(const QPointF &p_point, Qt::HitTestAccuracy p_accuracy) const
{
    Q_UNUSED(p_accuracy);
    int bn = findBlockByPosition(p_point);
    if (bn == -1) {
        return -1;
    }

    QTextBlock block = document()->findBlockByNumber(bn);
    Q_ASSERT(block.isValid());
    QTextLayout *layout = block.layout();
    int off = 0;
    QPointF pos = p_point - QPointF(m_margin, m_blocks[bn].top());
    for (int i = 0; i < layout->lineCount(); ++i) {
        QTextLine line = layout->lineAt(i);
        const QRectF lr = line.naturalTextRect();
        if (lr.top() > pos.y()) {
            off = qMin(off, line.textStart());
        } else if (lr.bottom() <= pos.y()) {
            off = qMax(off, line.textStart() + line.textLength());
        } else {
            off = line.xToCursor(pos.x(), QTextLine::CursorBetweenCharacters);
            break;
        }
    }

    return block.position() + off;
}

int VTextDocumentLayout::pageCount() const
{
    return 1;
}

QSizeF VTextDocumentLayout::documentSize() const
{
    return QSizeF(m_width, m_height);
}

QRectF VTextDocumentLayout::frameBoundingRect(QTextFrame *p_frame) const
{
    Q_UNUSED(p_frame);
    return QRectF(0, 0,
                  qMax(document()->pageSize().width(), m_width), qreal(INT_MAX));
}

QRectF VTextDocumentLayout::blockBoundingRect(const QTextBlock &p_block) const
{
    // Sometimes blockBoundingRect() maybe called before documentChanged().
    if (!p_block.isValid() || p_block.blockNumber() >= m_blocks.size()) {
        return QRectF();
    }

    const BlockInfo &info = m_blocks[p_block.blockNumber()];
    QRectF geo = info.m_rect.adjusted(0, info.m_offset, 0, info.m_offset);
    Q_ASSERT(info.hasOffset());

    return geo;
}

void VTextDocumentLayout::documentChanged(int p_from, int p_charsRemoved, int p_charsAdded)
{
    QTextDocument *doc = document();
    int newBlockCount = doc->blockCount();

    // Update the margin.
    m_margin = doc->documentMargin();

    int charsChanged = p_charsRemoved + p_charsAdded;

    QTextBlock changeStartBlock = doc->findBlock(p_from);
    // May be an invalid block.
    QTextBlock changeEndBlock = doc->findBlock(qMax(0, p_from + charsChanged));

    bool needRelayout = false;
    if (changeStartBlock == changeEndBlock
        && newBlockCount == m_blockCount) {
        // Change single block internal only.
        QTextBlock block = changeStartBlock;
        if (block.isValid() && block.length()) {
            QRectF oldBr = blockBoundingRect(block);
            clearBlockLayout(block);
            layoutBlock(block);
            QRectF newBr = blockBoundingRect(block);
            // Only one block is affected.
            if (newBr.height() == oldBr.height()) {
                // Update document size.
                updateDocumentSizeWithOneBlockChanged(block.blockNumber());

                emit updateBlock(block);
                return;
            }
        }
    } else {
        // Clear layout of all affected blocks.
        QTextBlock block = changeStartBlock;
        do {
            clearBlockLayout(block);
            if (block == changeEndBlock) {
                break;
            }

            block = block.next();
        } while(block.isValid());

        needRelayout = true;
    }

    updateBlockCount(newBlockCount, changeStartBlock.blockNumber());

    if (needRelayout) {
        // Relayout all affected blocks.
        QTextBlock block = changeStartBlock;
        do {
            layoutBlock(block);
            if (block == changeEndBlock) {
                break;
            }

            block = block.next();
        } while(block.isValid());
    }

    updateDocumentSize();

    // TODO: Update the view of all the blocks after changeStartBlock.
    const BlockInfo &firstInfo = m_blocks[changeStartBlock.blockNumber()];
    emit update(QRectF(0., firstInfo.m_offset, 1000000000., 1000000000.));
}

void VTextDocumentLayout::clearBlockLayout(QTextBlock &p_block)
{
    p_block.clearLayout();
    int num = p_block.blockNumber();
    if (num < m_blocks.size()) {
        m_blocks[num].reset();
        clearOffsetFrom(num + 1);
    }
}

void VTextDocumentLayout::clearOffsetFrom(int p_blockNumber)
{
    for (int i = p_blockNumber; i < m_blocks.size(); ++i) {
        if (!m_blocks[i].hasOffset()) {
            Q_ASSERT(validateBlocks());
            break;
        }

        m_blocks[i].m_offset = -1;
    }
}

void VTextDocumentLayout::fillOffsetFrom(int p_blockNumber)
{
    qreal offset = m_blocks[p_blockNumber].bottom();
    for (int i = p_blockNumber + 1; i < m_blocks.size(); ++i) {
        BlockInfo &info = m_blocks[i];
        if (!info.m_rect.isNull()) {
            info.m_offset = offset;
            offset += info.m_rect.height();
        } else {
            break;
        }
    }
}

bool VTextDocumentLayout::validateBlocks() const
{
    bool valid = true;
    for (int i = 0; i < m_blocks.size(); ++i) {
        const BlockInfo &info = m_blocks[i];
        if (!info.hasOffset()) {
            valid = false;
        } else if (!valid) {
            return false;
        }
    }

    return true;
}

void VTextDocumentLayout::updateBlockCount(int p_count, int p_changeStartBlock)
{
    if (m_blockCount != p_count) {
        m_blockCount = p_count;
        m_blocks.resize(m_blockCount);

        // Fix m_blocks.
        QTextBlock block = document()->findBlockByNumber(p_changeStartBlock);
        while (block.isValid()) {
            BlockInfo &info = m_blocks[block.blockNumber()];
            info.reset();

            QRectF br = blockRectFromTextLayout(block);
            if (!br.isNull()) {
                info.m_rect = br;
            }

            block = block.next();
        }
    }
}

void VTextDocumentLayout::layoutBlock(const QTextBlock &p_block)
{
    QTextDocument *doc = document();
    Q_ASSERT(m_margin == doc->documentMargin());

    QTextLayout *tl = p_block.layout();
    QTextOption option = doc->defaultTextOption();
    tl->setTextOption(option);

    int extraMargin = 0;
    if (option.flags() & QTextOption::AddSpaceForLineAndParagraphSeparators) {
        QFontMetrics fm(p_block.charFormat().font());
        extraMargin += fm.width(QChar(0x21B5));
    }

    qreal availableWidth = doc->pageSize().width();
    if (availableWidth <= 0) {
        availableWidth = qreal(INT_MAX);
    }

    availableWidth -= (2 * m_margin + extraMargin + m_cursorMargin + m_cursorWidth);

    QVector<Marker> markers;
    QVector<ImagePaintInfo> images;

    layoutLines(p_block, tl, markers, images, availableWidth, 0);

    // Set this block's line count to its layout's line count.
    // That is one block may occupy multiple visual lines.
    const_cast<QTextBlock&>(p_block).setLineCount(p_block.isVisible() ? tl->lineCount() : 0);

    // Update the info about this block.
    finishBlockLayout(p_block, markers, images);
}

qreal VTextDocumentLayout::layoutLines(const QTextBlock &p_block,
                                       QTextLayout *p_tl,
                                       QVector<Marker> &p_markers,
                                       QVector<ImagePaintInfo> &p_images,
                                       qreal p_availableWidth,
                                       qreal p_height)
{
    // Handle block inline image.
    bool hasInlineImages = false;
    const QVector<VPreviewInfo *> *info = NULL;
    if (m_blockImageEnabled) {
        VTextBlockData *blockData = dynamic_cast<VTextBlockData *>(p_block.userData());
        if (blockData) {
            info = &(blockData->getPreviews());
            if (!info->isEmpty()
                && info->first()->m_imageInfo.m_inline) {
                hasInlineImages = true;
            }
        }
    }

    p_tl->beginLayout();

    int imgIdx = 0;

    while (true) {
        QTextLine line = p_tl->createLine();
        if (!line.isValid()) {
            break;
        }

        line.setLeadingIncluded(true);
        line.setLineWidth(p_availableWidth);
        p_height += m_lineLeading;

        if (hasInlineImages) {
            QVector<const VPreviewedImageInfo *> images;
            QVector<QPair<qreal, qreal>> imageRange;
            qreal imgHeight = fetchInlineImagesForOneLine(*info,
                                                          &line,
                                                          m_margin,
                                                          imgIdx,
                                                          images,
                                                          imageRange);

            for (int i = 0; i < images.size(); ++i) {
                layoutInlineImage(images[i],
                                  p_height,
                                  imgHeight,
                                  imageRange[i].first,
                                  imageRange[i].second,
                                  p_markers,
                                  p_images);
            }

            if (!images.isEmpty()) {
                p_height += imgHeight + MARKER_THICKNESS + MARKER_THICKNESS;
            }
        }

        line.setPosition(QPointF(m_margin, p_height));
        p_height += line.height();
    }

    p_tl->endLayout();

    return p_height;
}

void VTextDocumentLayout::layoutInlineImage(const VPreviewedImageInfo *p_info,
                                            qreal p_heightInBlock,
                                            qreal p_imageSpaceHeight,
                                            qreal p_xStart,
                                            qreal p_xEnd,
                                            QVector<Marker> &p_markers,
                                            QVector<ImagePaintInfo> &p_images)
{
    Marker mk;
    qreal mky = p_imageSpaceHeight + p_heightInBlock + MARKER_THICKNESS;
    mk.m_start = QPointF(p_xStart, mky);
    mk.m_end = QPointF(p_xEnd, mky);
    p_markers.append(mk);

    if (p_info) {
        QSize size = p_info->m_imageSize;
        scaleSize(size, p_xEnd - p_xStart, p_imageSpaceHeight);

        ImagePaintInfo ipi;
        ipi.m_name = p_info->m_imageName;
        ipi.m_rect = QRectF(QPointF(p_xStart,
                                    p_heightInBlock + p_imageSpaceHeight - size.height()),
                            size);
        p_images.append(ipi);
    }
}

void VTextDocumentLayout::finishBlockLayout(const QTextBlock &p_block,
                                            const QVector<Marker> &p_markers,
                                            const QVector<ImagePaintInfo> &p_images)
{
    // Update rect and offset.
    Q_ASSERT(p_block.isValid());
    int num = p_block.blockNumber();
    Q_ASSERT(m_blocks.size() > num);
    ImagePaintInfo ipi;
    BlockInfo &info = m_blocks[num];
    info.reset();
    info.m_rect = blockRectFromTextLayout(p_block, &ipi);
    Q_ASSERT(!info.m_rect.isNull());
    int pre = previousValidBlockNumber(num);
    if (pre == -1) {
        info.m_offset = 0;
    } else if (m_blocks[pre].hasOffset()) {
        info.m_offset = m_blocks[pre].bottom();
    }

    bool hasImage = false;
    if (ipi.isValid()) {
        Q_ASSERT(p_markers.isEmpty());
        Q_ASSERT(p_images.isEmpty());
        info.m_images.append(ipi);
        hasImage = true;
    } else if (!p_markers.isEmpty()) {
        // Q_ASSERT(!p_images.isEmpty());
        info.m_markers = p_markers;
        info.m_images = p_images;
        hasImage = true;
    }

    // Add vertical marker.
    if (hasImage) {
        // Fill the marker.
        // Will be adjusted using offset.
        Marker mk;
        mk.m_start = QPointF(-1, 0);
        mk.m_end = QPointF(-1, info.m_rect.height());

        info.m_markers.append(mk);
    }

    if (info.hasOffset()) {
        fillOffsetFrom(num);
    }
}

int VTextDocumentLayout::previousValidBlockNumber(int p_number) const
{
    return p_number >= 0 ? p_number - 1 : -1;
}

int VTextDocumentLayout::nextValidBlockNumber(int p_number) const
{
    if (p_number <= -1) {
        return 0;
    } else if (p_number >= m_blocks.size() - 1) {
        return -1;
    } else {
        return p_number + 1;
    }
}

void VTextDocumentLayout::updateDocumentSize()
{
    // The last valid block.
    int idx = previousValidBlockNumber(m_blocks.size());
    Q_ASSERT(idx > -1);
    if (m_blocks[idx].hasOffset()) {
        int oldHeight = m_height;
        int oldWidth = m_width;

        m_height = m_blocks[idx].bottom();

        m_width = 0;
        for (int i = 0; i < m_blocks.size(); ++i) {
            const BlockInfo &info = m_blocks[i];
            Q_ASSERT(info.hasOffset());
            if (m_width < info.m_rect.width()) {
                m_width = info.m_rect.width();
                m_maximumWidthBlockNumber = i;
            }
        }

        if (oldHeight != m_height
            || oldWidth != m_width) {
            emit documentSizeChanged(documentSize());
        }
    }
}

void VTextDocumentLayout::setCursorWidth(int p_width)
{
    m_cursorWidth = p_width;
}

int VTextDocumentLayout::cursorWidth() const
{
    return m_cursorWidth;
}

QRectF VTextDocumentLayout::blockRectFromTextLayout(const QTextBlock &p_block,
                                                    ImagePaintInfo *p_image)
{
    if (p_image) {
        *p_image = ImagePaintInfo();
    }

    QTextLayout *tl = p_block.layout();
    if (tl->lineCount() < 1) {
        return QRectF();
    }

    QRectF tlRect = tl->boundingRect();
    QRectF br(QPointF(0, 0), tlRect.bottomRight());

    // Do not know why. Copied from QPlainTextDocumentLayout.
    if (tl->lineCount() == 1) {
        br.setWidth(qMax(br.width(), tl->lineAt(0).naturalTextWidth()));
    }

    // Handle block non-inline image.
    if (m_blockImageEnabled) {
        VTextBlockData *blockData = dynamic_cast<VTextBlockData *>(p_block.userData());
        if (blockData) {
            const QVector<VPreviewInfo *> &info = blockData->getPreviews();
            if (info.size() == 1) {
                const VPreviewedImageInfo& img = info.first()->m_imageInfo;
                if (!img.m_inline) {
                    int maximumWidth = tlRect.width();
                    int padding;
                    QSize size;
                    adjustImagePaddingAndSize(&img, maximumWidth, padding, size);

                    if (p_image) {
                        p_image->m_name = img.m_imageName;
                        p_image->m_rect = QRectF(padding + m_margin,
                                                 br.height() + m_lineLeading,
                                                 size.width(),
                                                 size.height());
                    }

                    int dw = padding + size.width() + m_margin - br.width();
                    int dh = size.height() + m_lineLeading;
                    br.adjust(0, 0, dw > 0 ? dw : 0, dh);
                }
            }
        }
    }

    br.adjust(0, 0, m_margin + m_cursorWidth, 0);

    // Add bottom margin.
    if (!p_block.next().isValid()) {
        br.adjust(0, 0, 0, m_margin);
    }

    return br;
}

void VTextDocumentLayout::updateDocumentSizeWithOneBlockChanged(int p_blockNumber)
{
    const BlockInfo &info = m_blocks[p_blockNumber];
    qreal width = info.m_rect.width();
    if (width > m_width) {
        m_width = width;
        m_maximumWidthBlockNumber = p_blockNumber;
        emit documentSizeChanged(documentSize());
    } else if (width < m_width && p_blockNumber == m_maximumWidthBlockNumber) {
        // Shrink the longest block.
        updateDocumentSize();
    }
}

void VTextDocumentLayout::setLineLeading(qreal p_leading)
{
    if (p_leading >= 0) {
        m_lineLeading = p_leading;
    }
}

void VTextDocumentLayout::setImageWidthConstrainted(bool p_enabled)
{
    if (m_imageWidthConstrainted == p_enabled) {
        return;
    }

    m_imageWidthConstrainted = p_enabled;
    relayout();
}

void VTextDocumentLayout::setBlockImageEnabled(bool p_enabled)
{
    if (m_blockImageEnabled == p_enabled) {
        return;
    }

    m_blockImageEnabled = p_enabled;
    relayout();
}

void VTextDocumentLayout::adjustImagePaddingAndSize(const VPreviewedImageInfo *p_info,
                                                    int p_maximumWidth,
                                                    int &p_padding,
                                                    QSize &p_size) const
{
    const int minimumImageWidth = 400;

    p_padding = p_info->m_padding;
    p_size = p_info->m_imageSize;

    if (!m_imageWidthConstrainted) {
        return;
    }

    int availableWidth = p_maximumWidth - p_info->m_padding;
    if (availableWidth < p_info->m_imageSize.width()) {
        // Need to resize the width.
        if (availableWidth >= minimumImageWidth) {
            p_size.scale(availableWidth, p_size.height(), Qt::KeepAspectRatio);
        } else {
            // Omit the padding.
            p_padding = 0;
            p_size.scale(p_maximumWidth, p_size.height(), Qt::KeepAspectRatio);
        }
    }
}

void VTextDocumentLayout::drawImages(QPainter *p_painter,
                                     const QTextBlock &p_block,
                                     const QPointF &p_offset)
{
    if (m_blocks.size() <= p_block.blockNumber()) {
        return;
    }

    const QVector<ImagePaintInfo> &images = m_blocks[p_block.blockNumber()].m_images;
    if (images.isEmpty()) {
        return;
    }

    for (auto const & img : images) {
        const QPixmap *image = m_imageMgr->findImage(img.m_name);
        if (!image) {
            continue;
        }

        QRect targetRect = img.m_rect.adjusted(p_offset.x(),
                                               p_offset.y(),
                                               p_offset.x(),
                                               p_offset.y()).toRect();

        p_painter->drawPixmap(targetRect, *image);
    }
}


void VTextDocumentLayout::drawMarkers(QPainter *p_painter,
                                      const QTextBlock &p_block,
                                      const QPointF &p_offset)
{
    if (m_blocks.size() <= p_block.blockNumber()) {
        return;
    }

    const QVector<Marker> &markers = m_blocks[p_block.blockNumber()].m_markers;
    if (markers.isEmpty()) {
        return;
    }

    QPen oldPen = p_painter->pen();
    QPen newPen(m_imageLineColor, MARKER_THICKNESS, Qt::DashLine);
    p_painter->setPen(newPen);

    for (auto const & mk : markers) {
        p_painter->drawLine(mk.m_start + p_offset, mk.m_end + p_offset);
    }

    p_painter->setPen(oldPen);
}

void VTextDocumentLayout::relayout()
{
    QTextDocument *doc = document();

    // Update the margin.
    m_margin = doc->documentMargin();

    QTextBlock block = doc->lastBlock();
    while (block.isValid()) {
        clearBlockLayout(block);
        block = block.previous();
    }

    block = doc->firstBlock();
    while (block.isValid()) {
        layoutBlock(block);
        block = block.next();
    }

    updateDocumentSize();

    emit update(QRectF(0., 0., 1000000000., 1000000000.));
}

void VTextDocumentLayout::relayout(const OrderedIntSet &p_blocks)
{
    if (p_blocks.isEmpty()) {
        return;
    }

    QTextDocument *doc = document();

    // Need to relayout and update blocks in ascending order.
    for (auto bn = p_blocks.keyBegin(); bn != p_blocks.keyEnd(); ++bn) {
        QTextBlock block = doc->findBlockByNumber(*bn);
        if (block.isValid()) {
            clearBlockLayout(block);
            layoutBlock(block);
            emit updateBlock(block);
        }
    }

    updateDocumentSize();
}

qreal VTextDocumentLayout::fetchInlineImagesForOneLine(const QVector<VPreviewInfo *> &p_info,
                                                       const QTextLine *p_line,
                                                       qreal p_margin,
                                                       int &p_index,
                                                       QVector<const VPreviewedImageInfo *> &p_images,
                                                       QVector<QPair<qreal, qreal>> &p_imageRange)
{
    qreal maxHeight = 0;
    int start = p_line->textStart();
    int end = p_line->textLength() + start;

    for (int i = 0; i < p_info.size(); ++i) {
        const VPreviewedImageInfo &img = p_info[i]->m_imageInfo;
        Q_ASSERT(img.m_inline);

        if (img.m_startPos >= start && img.m_startPos < end) {
            // Start of a new image.
            qreal startX = p_line->cursorToX(img.m_startPos) + p_margin;
            qreal endX;
            if (img.m_endPos <= end) {
                // End an image.
                endX = p_line->cursorToX(img.m_endPos) + p_margin;
                p_images.append(&img);
                p_imageRange.append(QPair<qreal, qreal>(startX, endX));

                QSize size = img.m_imageSize;
                scaleSize(size, endX - startX, MAX_INLINE_IMAGE_HEIGHT);
                if (size.height() > maxHeight) {
                    maxHeight = size.height();
                }

                // Image i has been drawn.
                p_index = i + 1;
            } else {
                // This image cross the line.
                endX = p_line->x() + p_line->width() + p_margin;
                if (end - img.m_startPos >= ((img.m_endPos - img.m_startPos) >> 1)) {
                    // Put image at this side.
                    p_images.append(&img);
                    p_imageRange.append(QPair<qreal, qreal>(startX, endX));

                    QSize size = img.m_imageSize;
                    scaleSize(size, endX - startX, MAX_INLINE_IMAGE_HEIGHT);
                    if (size.height() > maxHeight) {
                        maxHeight = size.height();
                    }

                    // Image i has been drawn.
                    p_index = i + 1;
                } else {
                    // Just put a marker here.
                    p_images.append(NULL);
                    p_imageRange.append(QPair<qreal, qreal>(startX, endX));
                }

                break;
            }
        } else if (img.m_endPos > start && img.m_startPos < start) {
            qreal startX = p_line->x() + p_margin;
            qreal endX = img.m_endPos > end ? p_line->x() + p_line->width()
                                            : p_line->cursorToX(img.m_endPos);
            if (p_index <= i) {
                // Image i has not been drawn. Draw it here.
                p_images.append(&img);
                p_imageRange.append(QPair<qreal, qreal>(startX, endX));

                QSize size = img.m_imageSize;
                scaleSize(size, endX - startX, MAX_INLINE_IMAGE_HEIGHT);
                if (size.height() > maxHeight) {
                    maxHeight = size.height();
                }

                // Image i has been drawn.
                p_index = i + 1;
            } else {
                // Image i has been drawn. Just put a marker here.
                p_images.append(NULL);
                p_imageRange.append(QPair<qreal, qreal>(startX, endX));
            }

            if (img.m_endPos >= end) {
                break;
            }
        } else if (img.m_endPos <= start) {
            continue;
        } else {
            break;
        }
    }

    return maxHeight;
}

int VTextDocumentLayout::getTextWidthWithinTextLine(const QTextLayout *p_layout,
                                                    int p_pos,
                                                    int p_length)
{
    QTextLine line = p_layout->lineForTextPosition(p_pos);
    Q_ASSERT(line.isValid());
    Q_ASSERT(p_pos + p_length <= line.textStart() + line.textLength());
    return line.cursorToX(p_pos + p_length) - line.cursorToX(p_pos);
}

void VTextDocumentLayout::updateBlockByNumber(int p_blockNumber)
{
    if (p_blockNumber == -1) {
        return;
    }

    QTextBlock block = document()->findBlockByNumber(p_blockNumber);
    if (block.isValid()) {
        emit updateBlock(block);
    }
}
