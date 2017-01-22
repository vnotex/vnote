#include <QtGui>
#include <QtDebug>
#include "hgmarkdownhighlighter.h"

const int HGMarkdownHighlighter::initCapacity = 1024;

void HGMarkdownHighlighter::resizeBuffer(int newCap)
{
    if (newCap == capacity) {
        return;
    }
    if (capacity > 0) {
        Q_ASSERT(content);
        delete [] content;
    }
    capacity = newCap;
    content = new char [capacity];
}

// Will be freeed by parent automatically
HGMarkdownHighlighter::HGMarkdownHighlighter(const QVector<HighlightingStyle> &styles, int waitInterval,
                                             QTextDocument *parent)
    : QSyntaxHighlighter(parent), parsing(0),
      waitInterval(waitInterval), content(NULL), capacity(0), result(NULL)
{
    codeBlockStartExp = QRegExp("^(\\s)*```");
    codeBlockEndExp = QRegExp("^(\\s)*```$");
    codeBlockFormat.setForeground(QBrush(Qt::darkYellow));
    for (int index = 0; index < styles.size(); ++index) {
        const pmh_element_type &eleType = styles[index].type;
        if (eleType == pmh_VERBATIM) {
            codeBlockFormat = styles[index].format;
        } else if (eleType == pmh_LINK) {
            m_linkFormat = styles[index].format;
        } else if (eleType == pmh_IMAGE) {
            m_imageFormat = styles[index].format;
        }
    }

    resizeBuffer(initCapacity);
    setStyles(styles);
    document = parent;
    timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(this->waitInterval);
    connect(timer, &QTimer::timeout, this, &HGMarkdownHighlighter::timerTimeout);
    connect(document, &QTextDocument::contentsChange,
            this, &HGMarkdownHighlighter::handleContentChange);
}

HGMarkdownHighlighter::~HGMarkdownHighlighter()
{
    if (result) {
        pmh_free_elements(result);
        result = NULL;
    }
    if (content) {
        delete [] content;
        capacity = 0;
        content = NULL;
    }
}

void HGMarkdownHighlighter::highlightBlock(const QString &text)
{
    int blockNum = currentBlock().blockNumber();
    if (!parsing && blockHighlights.size() > blockNum) {
        QVector<HLUnit> &units = blockHighlights[blockNum];
        for (int i = 0; i < units.size(); ++i) {
            // TODO: merge two format within the same range
            const HLUnit& unit = units[i];
            setFormat(unit.start, unit.length, highlightingStyles[unit.styleIndex].format);
        }
    }

    // We use PEG Markdown Highlight as the main highlighter.
    // We can use other highlighting methods to complement it.

    // PEG Markdown Highlight does not handle the ``` code block correctly.
    setCurrentBlockState(HighlightBlockState::Normal);
    highlightCodeBlock(text);

    // PEG Markdown Highlight does not handle links with spaces in the URL.
    highlightLinkWithSpacesInURL(text);
}

void HGMarkdownHighlighter::setStyles(const QVector<HighlightingStyle> &styles)
{
    this->highlightingStyles = styles;
}

void HGMarkdownHighlighter::initBlockHighlightFromResult(int nrBlocks)
{
    blockHighlights.resize(nrBlocks);
    for (int i = 0; i < blockHighlights.size(); ++i) {
        blockHighlights[i].clear();
    }

    if (!result) {
        return;
    }

    for (int i = 0; i < highlightingStyles.size(); i++)
    {
        const HighlightingStyle &style = highlightingStyles[i];
        pmh_element *elem_cursor = result[style.type];
        while (elem_cursor != NULL)
        {
            if (elem_cursor->end <= elem_cursor->pos) {
                elem_cursor = elem_cursor->next;
                continue;
            }
            initBlockHighlihgtOne(elem_cursor->pos, elem_cursor->end, i);
            elem_cursor = elem_cursor->next;
        }
    }

    updateImageBlocks();

    pmh_free_elements(result);
    result = NULL;
}

void HGMarkdownHighlighter::updateImageBlocks()
{
    imageBlocks.clear();
    for (int i = 0; i < highlightingStyles.size(); i++)
    {
        const HighlightingStyle &style = highlightingStyles[i];
        if (style.type != pmh_IMAGE) {
            continue;
        }
        pmh_element *elem_cursor = result[style.type];
        while (elem_cursor != NULL)
        {
            if (elem_cursor->end <= elem_cursor->pos) {
                elem_cursor = elem_cursor->next;
                continue;
            }

            int startBlock = document->findBlock(elem_cursor->pos).blockNumber();
            int endBlock = document->findBlock(elem_cursor->end).blockNumber();
            for (int i = startBlock; i <= endBlock; ++i) {
                imageBlocks.insert(i);
            }

            elem_cursor = elem_cursor->next;
        }
    }
    emit imageBlocksUpdated(imageBlocks);
}

void HGMarkdownHighlighter::initBlockHighlihgtOne(unsigned long pos, unsigned long end, int styleIndex)
{
    int startBlockNum = document->findBlock(pos).blockNumber();
    int endBlockNum = document->findBlock(end).blockNumber();
    for (int i = startBlockNum; i <= endBlockNum; ++i)
    {
        QTextBlock block = document->findBlockByNumber(i);
        int blockStartPos = block.position();
        HLUnit unit;
        if (i == startBlockNum) {
            unit.start = pos - blockStartPos;
            unit.length = (startBlockNum == endBlockNum) ?
                          (end - pos) : (block.length() - unit.start);
        } else if (i == endBlockNum) {
            unit.start = 0;
            unit.length = end - blockStartPos;
        } else {
            unit.start = 0;
            unit.length = block.length();
        }
        unit.styleIndex = styleIndex;

        blockHighlights[i].append(unit);
    }
}

void HGMarkdownHighlighter::highlightCodeBlock(const QString &text)
{
    int nextIndex = 0;
    int startIndex = 0;
    if (previousBlockState() != HighlightBlockState::CodeBlock) {
        startIndex = codeBlockStartExp.indexIn(text);
        if (startIndex >= 0) {
            nextIndex = startIndex + codeBlockStartExp.matchedLength();
        } else {
            nextIndex = -1;
        }
    }

    while (nextIndex >= 0) {
        int endIndex = codeBlockEndExp.indexIn(text, nextIndex);
        int codeBlockLength;
        if (endIndex == -1) {
            setCurrentBlockState(HighlightBlockState::CodeBlock);
            codeBlockLength = text.length() - startIndex;
        } else {
            codeBlockLength = endIndex - startIndex + codeBlockEndExp.matchedLength();
        }
        setFormat(startIndex, codeBlockLength, codeBlockFormat);
        startIndex = codeBlockStartExp.indexIn(text, startIndex + codeBlockLength);
        if (startIndex >= 0) {
            nextIndex = startIndex + codeBlockStartExp.matchedLength();
        } else {
            nextIndex = -1;
        }
    }
}

void HGMarkdownHighlighter::highlightLinkWithSpacesInURL(const QString &p_text)
{
    if (currentBlockState() == HighlightBlockState::CodeBlock) {
        return;
    }
    // TODO: should select links with spaces in URL.
    QRegExp regExp("[\\!]?\\[[^\\]]*\\]\\(([^\\n\\)]+)\\)");
    int index = regExp.indexIn(p_text);
    while (index >= 0) {
        Q_ASSERT(regExp.captureCount() == 1);
        int length = regExp.matchedLength();
        const QString &capturedText = regExp.capturedTexts()[1];
        if (capturedText.contains(' ')) {
            if (p_text[index] == '!' && m_imageFormat.isValid()) {
                setFormat(index, length, m_imageFormat);
            } else if (m_linkFormat.isValid()) {
                setFormat(index, length, m_linkFormat);
            }
        }
        index = regExp.indexIn(p_text, index + length);
    }
}

void HGMarkdownHighlighter::parse()
{
    if (!parsing.testAndSetRelaxed(0, 1)) {
        return;
    }

    int nrBlocks = document->blockCount();
    parseInternal();

    if (highlightingStyles.isEmpty()) {
        qWarning() << "error: HighlightingStyles is not set";
        return;
    }
    initBlockHighlightFromResult(nrBlocks);
    parsing.store(0);
}

void HGMarkdownHighlighter::parseInternal()
{
    QString text = document->toPlainText();
    QByteArray ba = text.toUtf8();
    const char *data = (const char *)ba.data();
   int len = ba.size();

    if (result) {
        pmh_free_elements(result);
        result = NULL;
    }

    if (len == 0) {
        return;
    } else if (len >= capacity) {
        resizeBuffer(qMax(2 * capacity, len * 2));
    } else if (len < (capacity >> 2)) {
        resizeBuffer(qMax(capacity >> 1, len * 2));
    }

    memcpy(content, data, len);
    content[len] = '\0';

    pmh_markdown_to_elements(content, pmh_EXT_NONE, &result);
}

void HGMarkdownHighlighter::handleContentChange(int /* position */, int charsRemoved, int charsAdded)
{
    if (charsRemoved == 0 && charsAdded == 0) {
        return;
    }
    timer->stop();
    timer->start();
}

void HGMarkdownHighlighter::timerTimeout()
{
    parse();
    rehighlight();
    emit highlightCompleted();
}

void HGMarkdownHighlighter::updateHighlight()
{
    timer->stop();
    timerTimeout();
}
