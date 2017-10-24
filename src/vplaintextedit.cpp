#include "vplaintextedit.h"

#include <QTextDocument>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QDebug>
#include <QScrollBar>

#include "vimageresourcemanager.h"


const int VPlainTextEdit::c_minimumImageWidth = 100;

enum class BlockState
{
    Normal = 1,
    CodeBlockStart,
    CodeBlock,
    CodeBlockEnd
};


VPlainTextEdit::VPlainTextEdit(QWidget *p_parent)
    : QPlainTextEdit(p_parent),
      m_imageMgr(NULL),
      m_blockImageEnabled(false),
      m_imageWidthConstrainted(false),
      m_maximumImageWidth(INT_MAX)
{
    init();
}

VPlainTextEdit::VPlainTextEdit(const QString &p_text, QWidget *p_parent)
    : QPlainTextEdit(p_text, p_parent),
      m_imageMgr(NULL),
      m_blockImageEnabled(false),
      m_imageWidthConstrainted(false),
      m_maximumImageWidth(INT_MAX)
{
    init();
}

VPlainTextEdit::~VPlainTextEdit()
{
    if (m_imageMgr) {
        delete m_imageMgr;
    }
}

void VPlainTextEdit::init()
{
    m_lineNumberType = LineNumberType::None;

    m_imageMgr = new VImageResourceManager();

    QTextDocument *doc = document();
    QPlainTextDocumentLayout *layout = new VPlainTextDocumentLayout(doc,
                                                                    m_imageMgr,
                                                                    m_blockImageEnabled);
    doc->setDocumentLayout(layout);

    m_lineNumberArea = new VLineNumberArea(this,
                                           document(),
                                           fontMetrics().width(QLatin1Char('8')),
                                           fontMetrics().height(),
                                           this);
    connect(document(), &QTextDocument::blockCountChanged,
            this, &VPlainTextEdit::updateLineNumberAreaMargin);
    connect(this, &QPlainTextEdit::textChanged,
            this, &VPlainTextEdit::updateLineNumberArea);
    connect(verticalScrollBar(), &QScrollBar::valueChanged,
            this, &VPlainTextEdit::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged,
            this, &VPlainTextEdit::updateLineNumberArea);
}

void VPlainTextEdit::updateBlockImages(const QVector<VBlockImageInfo> &p_blocksInfo)
{
    if (m_blockImageEnabled) {
        m_imageMgr->updateBlockInfos(p_blocksInfo, m_maximumImageWidth);
    }
}

void VPlainTextEdit::clearBlockImages()
{
    m_imageMgr->clear();
}

bool VPlainTextEdit::containsImage(const QString &p_imageName) const
{
    return m_imageMgr->contains(p_imageName);
}

void VPlainTextEdit::addImage(const QString &p_imageName, const QPixmap &p_image)
{
    if (m_blockImageEnabled) {
        m_imageMgr->addImage(p_imageName, p_image);
    }
}

static void fillBackground(QPainter *p,
                           const QRectF &rect,
                           QBrush brush,
                           const QRectF &gradientRect = QRectF())
{
    p->save();
    if (brush.style() >= Qt::LinearGradientPattern
        && brush.style() <= Qt::ConicalGradientPattern) {
        if (!gradientRect.isNull()) {
            QTransform m = QTransform::fromTranslate(gradientRect.left(),
                                                     gradientRect.top());
            m.scale(gradientRect.width(),
                    gradientRect.height());
            brush.setTransform(m);
            const_cast<QGradient *>(brush.gradient())->setCoordinateMode(QGradient::LogicalMode);
        }
    } else {
        p->setBrushOrigin(rect.topLeft());
    }

    p->fillRect(rect, brush);
    p->restore();
}

void VPlainTextEdit::paintEvent(QPaintEvent *p_event)
{
    QPainter painter(viewport());
    QPointF offset(contentOffset());

    QRect er = p_event->rect();
    QRect viewportRect = viewport()->rect();

    bool editable = !isReadOnly();

    QTextBlock block = firstVisibleBlock();
    qreal maximumWidth = document()->documentLayout()->documentSize().width();

    // Set a brush origin so that the WaveUnderline knows where the wave started.
    painter.setBrushOrigin(offset);

    // Keep right margin clean from full-width selection.
    int maxX = offset.x() + qMax((qreal)viewportRect.width(), maximumWidth)
               - document()->documentMargin();
    er.setRight(qMin(er.right(), maxX));
    painter.setClipRect(er);

    QAbstractTextDocumentLayout::PaintContext context = getPaintContext();

    while (block.isValid()) {
        QRectF r = blockBoundingRect(block).translated(offset);
        QTextLayout *layout = block.layout();

        if (!block.isVisible()) {
            offset.ry() += r.height();
            block = block.next();
            continue;
        }

        if (r.bottom() >= er.top() && r.top() <= er.bottom()) {
            QTextBlockFormat blockFormat = block.blockFormat();
            QBrush bg = blockFormat.background();
            if (bg != Qt::NoBrush) {
                QRectF contentsRect = r;
                contentsRect.setWidth(qMax(r.width(), maximumWidth));
                fillBackground(&painter, contentsRect, bg);
            }

            QVector<QTextLayout::FormatRange> selections;
            int blpos = block.position();
            int bllen = block.length();
            for (int i = 0; i < context.selections.size(); ++i) {
                const QAbstractTextDocumentLayout::Selection &range = context.selections.at(i);
                const int selStart = range.cursor.selectionStart() - blpos;
                const int selEnd = range.cursor.selectionEnd() - blpos;
                if (selStart < bllen
                    && selEnd > 0
                    && selEnd > selStart) {
                    QTextLayout::FormatRange o;
                    o.start = selStart;
                    o.length = selEnd - selStart;
                    o.format = range.format;
                    selections.append(o);
                } else if (!range.cursor.hasSelection()
                           && range.format.hasProperty(QTextFormat::FullWidthSelection)
                           && block.contains(range.cursor.position())) {
                    // For full width selections we don't require an actual selection, just
                    // a position to specify the line. That's more convenience in usage.
                    QTextLayout::FormatRange o;
                    QTextLine l = layout->lineForTextPosition(range.cursor.position() - blpos);
                    o.start = l.textStart();
                    o.length = l.textLength();
                    if (o.start + o.length == bllen - 1) {
                        ++o.length; // include newline
                    }

                    o.format = range.format;
                    selections.append(o);
                }
            }

            bool drawCursor = (editable
                               || (textInteractionFlags() & Qt::TextSelectableByKeyboard))
                              && context.cursorPosition >= blpos
                              && context.cursorPosition < blpos + bllen;

            bool drawCursorAsBlock = drawCursor && overwriteMode() ;
            if (drawCursorAsBlock) {
                if (context.cursorPosition == blpos + bllen - 1) {
                    drawCursorAsBlock = false;
                } else {
                    QTextLayout::FormatRange o;
                    o.start = context.cursorPosition - blpos;
                    o.length = 1;
                    o.format.setForeground(palette().base());
                    o.format.setBackground(palette().text());
                    selections.append(o);
                }
            }

            if (!placeholderText().isEmpty()
                && document()->isEmpty()
                && layout->preeditAreaText().isEmpty()) {
                  QColor col = palette().text().color();
                  col.setAlpha(128);
                  painter.setPen(col);
                  const int margin = int(document()->documentMargin());
                  painter.drawText(r.adjusted(margin, 0, 0, 0),
                                   Qt::AlignTop | Qt::TextWordWrap,
                                   placeholderText());
            } else {
                layout->draw(&painter, offset, selections, er);
            }

            if ((drawCursor && !drawCursorAsBlock)
                || (editable
                    && context.cursorPosition < -1
                    && !layout->preeditAreaText().isEmpty())) {
                int cpos = context.cursorPosition;
                if (cpos < -1) {
                    cpos = layout->preeditAreaPosition() - (cpos + 2);
                } else {
                    cpos -= blpos;
                }

                layout->drawCursor(&painter, offset, cpos, cursorWidth());
            }

            // Draw preview image of this block if there is one.
            drawImageOfBlock(block, &painter, r);
        }

        offset.ry() += r.height();
        if (offset.y() > viewportRect.height()) {
            break;
        }

        block = block.next();
    }

    if (backgroundVisible()
        && !block.isValid()
        && offset.y() <= er.bottom()
        && (centerOnScroll()
            || verticalScrollBar()->maximum() == verticalScrollBar()->minimum())) {
        painter.fillRect(QRect(QPoint((int)er.left(),
                               (int)offset.y()),
                               er.bottomRight()),
                         palette().background());
    }
}

void VPlainTextEdit::drawImageOfBlock(const QTextBlock &p_block,
                                      QPainter *p_painter,
                                      const QRectF &p_blockRect)
{
    if (!m_blockImageEnabled) {
        return;
    }

    const VBlockImageInfo *info = m_imageMgr->findImageInfoByBlock(p_block.blockNumber());
    if (!info) {
        return;
    }

    const QPixmap *image = m_imageMgr->findImage(info->m_imageName);
    if (!image) {
        return;
    }

    int oriHeight = originalBlockBoundingRect(p_block).height();
    bool noMargin = (info->m_margin + info->m_imageWidth > m_maximumImageWidth);
    int margin = noMargin ? 0 : info->m_margin;
    QRect tmpRect(p_blockRect.toRect());
    QRect targetRect(tmpRect.x() + margin,
                     tmpRect.y() + oriHeight,
                     info->m_imageWidth,
                     qMax(info->m_imageHeight, tmpRect.height() - oriHeight));

    p_painter->drawPixmap(targetRect, *image);
}

QRectF VPlainTextEdit::originalBlockBoundingRect(const QTextBlock &p_block) const
{
    return getLayout()->QPlainTextDocumentLayout::blockBoundingRect(p_block);
}

void VPlainTextEdit::setBlockImageEnabled(bool p_enabled)
{
    if (m_blockImageEnabled == p_enabled) {
        return;
    }

    m_blockImageEnabled = p_enabled;
    if (!p_enabled) {
        clearBlockImages();
    }

    getLayout()->setBlockImageEnabled(m_blockImageEnabled);
}

void VPlainTextEdit::setImageWidthConstrainted(bool p_enabled)
{
    m_imageWidthConstrainted = p_enabled;
}

void VPlainTextEdit::resizeEvent(QResizeEvent *p_event)
{
    bool needUpdate = false;
    if (m_imageWidthConstrainted) {
        const QSize &si = p_event->size();
        m_maximumImageWidth = si.width();
        needUpdate = true;
    } else if (m_maximumImageWidth != INT_MAX) {
        needUpdate = true;
        m_maximumImageWidth = INT_MAX;
    }

    if (needUpdate) {
        m_imageMgr->updateImageWidth(m_maximumImageWidth);
    }

    QPlainTextEdit::resizeEvent(p_event);

    if (m_lineNumberType != LineNumberType::None) {
        QRect rect = contentsRect();
        m_lineNumberArea->setGeometry(QRect(rect.left(),
                                            rect.top(),
                                            m_lineNumberArea->calculateWidth(),
                                            rect.height()));
    }
}

void VPlainTextEdit::paintLineNumberArea(QPaintEvent *p_event)
{
    if (m_lineNumberType == LineNumberType::None) {
        updateLineNumberAreaMargin();
        m_lineNumberArea->hide();
        return;
    }

    QPainter painter(m_lineNumberArea);
    painter.fillRect(p_event->rect(), m_lineNumberArea->getBackgroundColor());

    QTextBlock block = firstVisibleBlock();
    if (!block.isValid()) {
        return;
    }

    int blockNumber = block.blockNumber();
    int offsetY = (int)contentOffset().y();
    QRectF rect = blockBoundingRect(block);
    int top = offsetY + (int)rect.y();
    int bottom = top + (int)rect.height();
    int eventTop = p_event->rect().top();
    int eventBtm = p_event->rect().bottom();
    const int digitHeight = m_lineNumberArea->getDigitHeight();
    const int curBlockNumber = textCursor().block().blockNumber();
    painter.setPen(m_lineNumberArea->getForegroundColor());

    // Display line number only in code block.
    if (m_lineNumberType == LineNumberType::CodeBlock) {
        int number = 0;
        while (block.isValid() && top <= eventBtm) {
            int blockState = block.userState();
            switch (blockState) {
            case (int)BlockState::CodeBlockStart:
                Q_ASSERT(number == 0);
                number = 1;
                break;

            case (int)BlockState::CodeBlockEnd:
                number = 0;
                break;

            case (int)BlockState::CodeBlock:
                if (number == 0) {
                    // Need to find current line number in code block.
                    QTextBlock startBlock = block.previous();
                    while (startBlock.isValid()) {
                        if (startBlock.userState() == (int)BlockState::CodeBlockStart) {
                            number = block.blockNumber() - startBlock.blockNumber();
                            break;
                        }

                        startBlock = startBlock.previous();
                    }
                }

                break;

            default:
                break;
            }

            if (blockState == (int)BlockState::CodeBlock) {
                if (block.isVisible() && bottom >= eventTop) {
                    QString numberStr = QString::number(number);
                    painter.drawText(0,
                                     top,
                                     m_lineNumberArea->width(),
                                     digitHeight,
                                     Qt::AlignRight,
                                     numberStr);
                }

                ++number;
            }

            block = block.next();
            top = bottom;
            bottom = top + (int)blockBoundingRect(block).height();
        }

        return;
    }

    // Handle m_lineNumberType 1 and 2.
    Q_ASSERT(m_lineNumberType == LineNumberType::Absolute
             || m_lineNumberType == LineNumberType::Relative);
    while (block.isValid() && top <= eventBtm) {
        if (block.isVisible() && bottom >= eventTop) {
            bool currentLine = false;
            int number = blockNumber + 1;
            if (m_lineNumberType == LineNumberType::Relative) {
                number = blockNumber - curBlockNumber;
                if (number == 0) {
                    currentLine = true;
                    number = blockNumber + 1;
                } else if (number < 0) {
                    number = -number;
                }
            } else if (blockNumber == curBlockNumber) {
                currentLine = true;
            }

            QString numberStr = QString::number(number);

            if (currentLine) {
                QFont font = painter.font();
                font.setBold(true);
                painter.setFont(font);
            }

            painter.drawText(0,
                             top,
                             m_lineNumberArea->width(),
                             digitHeight,
                             Qt::AlignRight,
                             numberStr);

            if (currentLine) {
                QFont font = painter.font();
                font.setBold(false);
                painter.setFont(font);
            }
        }

        block = block.next();
        top = bottom;
        bottom = top + (int)blockBoundingRect(block).height();
        ++blockNumber;
    }

}

VPlainTextDocumentLayout *VPlainTextEdit::getLayout() const
{
    return qobject_cast<VPlainTextDocumentLayout *>(document()->documentLayout());
}

void VPlainTextEdit::updateLineNumberAreaMargin()
{
    int width = 0;
    if (m_lineNumberType != LineNumberType::None) {
        width = m_lineNumberArea->calculateWidth();
    }

    setViewportMargins(width, 0, 0, 0);
}

void VPlainTextEdit::updateLineNumberArea()
{
    if (m_lineNumberType != LineNumberType::None) {
        if (!m_lineNumberArea->isVisible()) {
            updateLineNumberAreaMargin();
            m_lineNumberArea->show();
        }

        m_lineNumberArea->update();
    } else if (m_lineNumberArea->isVisible()) {
        updateLineNumberAreaMargin();
        m_lineNumberArea->hide();
    }
}

VPlainTextDocumentLayout::VPlainTextDocumentLayout(QTextDocument *p_document,
                                                   VImageResourceManager *p_imageMgr,
                                                   bool p_blockImageEnabled)
    : QPlainTextDocumentLayout(p_document),
      m_imageMgr(p_imageMgr),
      m_blockImageEnabled(p_blockImageEnabled),
      m_maximumImageWidth(INT_MAX)
{
}

QRectF VPlainTextDocumentLayout::blockBoundingRect(const QTextBlock &p_block) const
{
    QRectF br = QPlainTextDocumentLayout::blockBoundingRect(p_block);
    if (!m_blockImageEnabled) {
        return br;
    }

    const VBlockImageInfo *info = m_imageMgr->findImageInfoByBlock(p_block.blockNumber());
    if (info) {
        int tmp = info->m_margin + info->m_imageWidth;
        if (tmp > m_maximumImageWidth) {
            Q_ASSERT(info->m_imageWidth <= m_maximumImageWidth);
            tmp = info->m_imageWidth;
        }

        qreal width = (qreal)(tmp);
        qreal dw = width > br.width() ? width - br.width() : 0;
        qreal dh = (qreal)info->m_imageHeight;

        br.adjust(0, 0, dw, dh);
    }

    return br;
}

QRectF VPlainTextDocumentLayout::frameBoundingRect(QTextFrame *p_frame) const
{
    QRectF fr = QPlainTextDocumentLayout::frameBoundingRect(p_frame);
    if (!m_blockImageEnabled) {
        return fr;
    }

    qreal imageWidth = (qreal)m_imageMgr->getMaximumImageWidth();
    qreal dw = imageWidth - fr.width();
    if (dw > 0) {
        fr.adjust(0, 0, dw, 0);
    }

    return fr;
}

QSizeF VPlainTextDocumentLayout::documentSize() const
{
    QSizeF si = QPlainTextDocumentLayout::documentSize();
    if (!m_blockImageEnabled) {
        return si;
    }

    qreal imageWidth = (qreal)m_imageMgr->getMaximumImageWidth();
    if (imageWidth > si.width()) {
        si.setWidth(imageWidth);
    }

    return si;
}
