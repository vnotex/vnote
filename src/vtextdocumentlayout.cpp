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
      m_imageLineColor("#9575CD")
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
            fillBackground(p_painter, rect, bg);
        }

        auto selections = formatRangeFromSelection(block, p_context.selections);

        layout->draw(p_painter,
                     offset,
                     selections,
                     p_context.clip.isValid() ? p_context.clip : QRectF());

        drawBlockImage(p_painter, block, offset);

        // Draw the cursor.
        int blpos = block.position();
        int bllen = block.length();
        bool drawCursor = p_context.cursorPosition >= blpos
                          && p_context.cursorPosition < blpos + bllen;
        if (drawCursor
            || (p_context.cursorPosition < -1
                && !layout->preeditAreaText().isEmpty())) {
            int cpos = p_context.cursorPosition;
            if (cpos < -1) {
                cpos = layout->preeditAreaPosition() - (cpos + 2);
            } else {
                cpos -= blpos;
            }

            layout->drawCursor(p_painter, offset, cpos, m_cursorWidth);
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

    // The height (y) of the next line.
    qreal height = 0;
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

    availableWidth -= (2 * m_margin + extraMargin + m_cursorMargin);

    tl->beginLayout();

    while (true) {
        QTextLine line = tl->createLine();
        if (!line.isValid()) {
            break;
        }

        line.setLeadingIncluded(true);
        line.setLineWidth(availableWidth);
        height += m_lineLeading;
        line.setPosition(QPointF(m_margin, height));
        height += line.height();
    }

    tl->endLayout();

    // Set this block's line count to its layout's line count.
    // That is one block may occupy multiple visual lines.
    const_cast<QTextBlock&>(p_block).setLineCount(p_block.isVisible() ? tl->lineCount() : 0);

    // Update the info about this block.
    finishBlockLayout(p_block);
}

void VTextDocumentLayout::finishBlockLayout(const QTextBlock &p_block)
{
    // Update rect and offset.
    Q_ASSERT(p_block.isValid());
    int num = p_block.blockNumber();
    Q_ASSERT(m_blocks.size() > num);
    BlockInfo &info = m_blocks[num];
    info.reset();
    info.m_rect = blockRectFromTextLayout(p_block);
    Q_ASSERT(!info.m_rect.isNull());
    int pre = previousValidBlockNumber(num);
    if (pre == -1) {
        info.m_offset = 0;
    } else if (m_blocks[pre].hasOffset()) {
        info.m_offset = m_blocks[pre].bottom();
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

QRectF VTextDocumentLayout::blockRectFromTextLayout(const QTextBlock &p_block)
{
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

    // Handle block image.
    if (m_blockImageEnabled) {
        const VBlockImageInfo2 *info = m_imageMgr->findImageInfoByBlock(p_block.blockNumber());
        if (info && !info->m_imageSize.isNull()) {
            int maximumWidth = tlRect.width();
            int padding;
            QSize size;
            adjustImagePaddingAndSize(info, maximumWidth, padding, size);
            int dw = padding + size.width() + m_margin - br.width();
            int dh = size.height() + m_lineLeading;
            br.adjust(0, 0, dw > 0 ? dw : 0, dh);
        }
    }

    br.adjust(0, 0, m_margin + m_cursorMargin, 0);

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

void VTextDocumentLayout::adjustImagePaddingAndSize(const VBlockImageInfo2 *p_info,
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

void VTextDocumentLayout::drawBlockImage(QPainter *p_painter,
                                         const QTextBlock &p_block,
                                         const QPointF &p_offset)
{
    if (!m_blockImageEnabled) {
        return;
    }

    const VBlockImageInfo2 *info = m_imageMgr->findImageInfoByBlock(p_block.blockNumber());
    if (!info || info->m_imageSize.isNull()) {
        return;
    }

    const QPixmap *image = m_imageMgr->findImage(info->m_imageName);
    Q_ASSERT(image);

    // Draw block image.
    QTextLayout *tl = p_block.layout();
    QRectF tlRect = tl->boundingRect();
    int maximumWidth = tlRect.width();
    int padding;
    QSize size;
    adjustImagePaddingAndSize(info, maximumWidth, padding, size);
    QRect targetRect(p_offset.x() + padding,
                     p_offset.y() + tlRect.height() + m_lineLeading,
                     size.width(),
                     size.height());

    p_painter->drawPixmap(targetRect, *image);

    // Draw a thin line to link them.
    QPen oldPen = p_painter->pen();
    QPen newPen(m_imageLineColor, 2, Qt::DashLine);
    p_painter->setPen(newPen);
    p_painter->drawLine(QPointF(2, p_offset.y()), QPointF(2, targetRect.bottom()));
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

void VTextDocumentLayout::relayout(const QSet<int> &p_blocks)
{
    if (p_blocks.isEmpty()) {
        return;
    }

    QTextDocument *doc = document();

    for (auto bn : p_blocks) {
        QTextBlock block = doc->findBlockByNumber(bn);
        if (block.isValid()) {
            clearBlockLayout(block);
            layoutBlock(block);
            emit updateBlock(block);
        }
    }

    updateDocumentSize();
}
