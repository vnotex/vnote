#include <QtGui>
#include <QtDebug>
#include <QTextCursor>
#include <algorithm>
#include "hgmarkdownhighlighter.h"
#include "vconfigmanager.h"

extern VConfigManager vconfig;

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
HGMarkdownHighlighter::HGMarkdownHighlighter(const QVector<HighlightingStyle> &styles,
                                             const QMap<QString, QTextCharFormat> &codeBlockStyles,
                                             int waitInterval,
                                             QTextDocument *parent)
    : QSyntaxHighlighter(parent), highlightingStyles(styles),
      m_codeBlockStyles(codeBlockStyles), m_numOfCodeBlockHighlightsToRecv(0),
      parsing(0), waitInterval(waitInterval), content(NULL), capacity(0), result(NULL)
{
    codeBlockStartExp = QRegExp("^\\s*```(\\S*)");
    codeBlockEndExp = QRegExp("^\\s*```$");
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
        const QVector<HLUnit> &units = blockHighlights[blockNum];
        for (int i = 0; i < units.size(); ++i) {
            // TODO: merge two format within the same range
            const HLUnit &unit = units[i];
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

    // Highlight CodeBlock using VCodeBlockHighlightHelper.
    if (m_codeBlockHighlights.size() > blockNum) {
        const QVector<HLUnitStyle> &units = m_codeBlockHighlights[blockNum];
        // Manually simply merge the format of all the units within the same block.
        // Using QTextCursor to get the char format after setFormat() seems
        // not to work.
        QVector<QTextCharFormat> formats;
        formats.reserve(units.size());
        // formatIndex[i] is the index in @formats which is the format of the
        // ith character.
        QVector<int> formatIndex(currentBlock().length(), -1);
        for (int i = 0; i < units.size(); ++i) {
            const HLUnitStyle &unit = units[i];
            auto it = m_codeBlockStyles.find(unit.style);
            if (it != m_codeBlockStyles.end()) {
                QTextCharFormat newFormat;
                if (unit.start < (unsigned int)formatIndex.size() && formatIndex[unit.start] != -1) {
                    newFormat = formats[formatIndex[unit.start]];
                    newFormat.merge(*it);
                } else {
                    newFormat = *it;
                }
                setFormat(unit.start, unit.length, newFormat);

                formats.append(newFormat);
                int idx = formats.size() - 1;
                unsigned int endIdx = unit.length + unit.start;
                for (unsigned int i = unit.start; i < endIdx && i < (unsigned int)formatIndex.size(); ++i) {
                    formatIndex[i] = idx;
                }
            }
        }
    }
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
        QString capturedText = regExp.capturedTexts()[1];
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
        qWarning() << "HighlightingStyles is not set";
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
    if (!updateCodeBlocks()) {
        rehighlight();
    }
    emit highlightCompleted();
}

void HGMarkdownHighlighter::updateHighlight()
{
    timer->stop();
    timerTimeout();
}

bool HGMarkdownHighlighter::updateCodeBlocks()
{
    if (!vconfig.getEnableCodeBlockHighlight()) {
        m_codeBlockHighlights.clear();
        return false;
    }

    m_codeBlockHighlights.resize(document->blockCount());
    for (int i = 0; i < m_codeBlockHighlights.size(); ++i) {
        m_codeBlockHighlights[i].clear();
    }

    QList<VCodeBlock> codeBlocks;

    VCodeBlock item;
    bool inBlock = false;

    // Only handle complete codeblocks.
    QTextBlock block = document->firstBlock();
    while (block.isValid()) {
        QString text = block.text();
        if (inBlock) {
            item.m_text = item.m_text + "\n" + text;
            int idx = codeBlockEndExp.indexIn(text);
            if (idx >= 0) {
                // End block.
                inBlock = false;
                item.m_endBlock = block.blockNumber();
                codeBlocks.append(item);
            }
        } else {
            int idx = codeBlockStartExp.indexIn(text);
            if (idx >= 0) {
                // Start block.
                inBlock = true;
                item.m_startBlock = block.blockNumber();
                item.m_startPos = block.position();
                item.m_text = text;
                if (codeBlockStartExp.captureCount() == 1) {
                    item.m_lang = codeBlockStartExp.capturedTexts()[1];
                }
            }
        }
        block = block.next();
    }

    m_numOfCodeBlockHighlightsToRecv = codeBlocks.size();
    if (m_numOfCodeBlockHighlightsToRecv > 0) {
        emit codeBlocksUpdated(codeBlocks);
        return true;
    } else {
        return false;
    }
}

static bool HLUnitStyleComp(const HLUnitStyle &a, const HLUnitStyle &b)
{
    if (a.start < b.start) {
        return true;
    } else if (a.start == b.start) {
        return a.length > b.length;
    } else {
        return false;
    }
}

void HGMarkdownHighlighter::setCodeBlockHighlights(const QList<HLUnitPos> &p_units)
{
    if (p_units.isEmpty()) {
        goto exit;
    }

    {
    QVector<QVector<HLUnitStyle>> highlights(m_codeBlockHighlights.size());

    for (auto const &unit : p_units) {
        int pos = unit.m_position;
        int end = unit.m_position + unit.m_length;
        int startBlockNum = document->findBlock(pos).blockNumber();
        int endBlockNum = document->findBlock(end).blockNumber();
        for (int i = startBlockNum; i <= endBlockNum; ++i)
        {
            QTextBlock block = document->findBlockByNumber(i);
            int blockStartPos = block.position();
            HLUnitStyle hl;
            hl.style = unit.m_style;
            if (i == startBlockNum) {
                hl.start = pos - blockStartPos;
                hl.length = (startBlockNum == endBlockNum) ?
                                (end - pos) : (block.length() - hl.start);
            } else if (i == endBlockNum) {
                hl.start = 0;
                hl.length = end - blockStartPos;
            } else {
                hl.start = 0;
                hl.length = block.length();
            }

            highlights[i].append(hl);
        }
    }

    // Need to highlight in order.
    for (int i = 0; i < highlights.size(); ++i) {
        QVector<HLUnitStyle> &units = highlights[i];
        if (!units.isEmpty()) {
            std::sort(units.begin(), units.end(), HLUnitStyleComp);
            m_codeBlockHighlights[i].append(units);
        }
    }
    }

exit:
    --m_numOfCodeBlockHighlightsToRecv;
    if (m_numOfCodeBlockHighlightsToRecv <= 0) {
        rehighlight();
    }
}
