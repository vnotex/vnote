#include <QtGui>
#include <QtDebug>
#include <QTextCursor>
#include <algorithm>
#include "hgmarkdownhighlighter.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"

extern VConfigManager *g_config;

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
                                             const QHash<QString, QTextCharFormat> &codeBlockStyles,
                                             int waitInterval,
                                             QTextDocument *parent)
    : QSyntaxHighlighter(parent),
      highlightingStyles(styles),
      m_codeBlockStyles(codeBlockStyles),
      m_numOfCodeBlockHighlightsToRecv(0),
      parsing(0),
      m_blockHLResultReady(false),
      waitInterval(waitInterval),
      m_enableMathjax(false),
      m_signalOut(false),
      content(NULL),
      capacity(0),
      result(NULL)
{
    codeBlockStartExp = QRegExp(VUtils::c_fencedCodeBlockStartRegExp);
    codeBlockEndExp = QRegExp(VUtils::c_fencedCodeBlockEndRegExp);

    m_mathjaxInlineExp = QRegExp(VUtils::c_mathjaxInlineRegExp);
    m_mathjaxBlockExp = QRegExp(VUtils::c_mathjaxBlockRegExp);

    m_codeBlockFormat.setForeground(QBrush(Qt::darkYellow));
    for (int index = 0; index < styles.size(); ++index) {
        const pmh_element_type &eleType = styles[index].type;
        if (eleType == pmh_VERBATIM) {
            m_codeBlockFormat = styles[index].format;
        } else if (eleType == pmh_LINK) {
            m_linkFormat = styles[index].format;
        } else if (eleType == pmh_IMAGE) {
            m_imageFormat = styles[index].format;
        }
    }

    m_colorColumnFormat = m_codeBlockFormat;
    m_colorColumnFormat.setForeground(QColor(g_config->getEditorColorColumnFg()));
    m_colorColumnFormat.setBackground(QColor(g_config->getEditorColorColumnBg()));

    m_mathjaxFormat.setForeground(QColor(g_config->getEditorMathjaxFg()));

    m_headerStyles.resize(6);
    for (auto const & it : highlightingStyles) {
        if (it.type >= pmh_H1 && it.type <= pmh_H6) {
            m_headerStyles[it.type - pmh_H1] = it.format;
        }
    }

    resizeBuffer(initCapacity);
    document = parent;

    timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(this->waitInterval);
    connect(timer, &QTimer::timeout,
            this, [this]() {
                startParseAndHighlight(false);
            });

    const int completeWaitTime = 400;
    m_completeTimer = new QTimer(this);
    m_completeTimer->setSingleShot(true);
    m_completeTimer->setInterval(completeWaitTime);
    connect(m_completeTimer, &QTimer::timeout,
            this, &HGMarkdownHighlighter::completeHighlight);

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

void HGMarkdownHighlighter::updateBlockUserData(int p_blockNum, const QString &p_text)
{
    Q_UNUSED(p_text);

    VTextBlockData *blockData = currentBlockData();
    if (!blockData) {
        blockData = new VTextBlockData();
        setCurrentBlockUserData(blockData);
    } else {
        blockData->setCodeBlockIndentation(-1);
        blockData->clearMathjax();
    }

    if (blockData->getPreviews().isEmpty()) {
        m_possiblePreviewBlocks.remove(p_blockNum);
    } else {
        m_possiblePreviewBlocks.insert(p_blockNum);
    }
}

void HGMarkdownHighlighter::highlightBlock(const QString &text)
{
    QTextBlock curBlock = currentBlock();
    int blockNum = curBlock.blockNumber();
    if (m_blockHLResultReady && m_blockHighlights.size() > blockNum) {
        // units are sorted by start position and length.
        const QVector<HLUnit> &units = m_blockHighlights[blockNum];
        if (!units.isEmpty()) {
            for (int i = 0; i < units.size(); ++i) {
                const HLUnit &unit = units[i];
                if (i == 0) {
                    // No need to merge format.
                    setFormat(unit.start,
                              unit.length,
                              highlightingStyles[unit.styleIndex].format);
                } else {
                    QTextCharFormat newFormat = highlightingStyles[unit.styleIndex].format;
                    for (int j = i - 1; j >= 0; --j) {
                        if (units[j].start + units[j].length <= unit.start) {
                            // It won't affect current unit.
                            continue;
                        } else {
                            // Merge the format.
                            QTextCharFormat tmpFormat(newFormat);
                            newFormat = highlightingStyles[units[j].styleIndex].format;
                            // tmpFormat takes precedence.
                            newFormat.merge(tmpFormat);
                        }
                    }

                    setFormat(unit.start, unit.length, newFormat);
                }
            }
        }
    }

    // We use PEG Markdown Highlight as the main highlighter.
    // We can use other highlighting methods to complement it.

    // Set current block's user data.
    updateBlockUserData(blockNum, text);

    // If it is a block inside HTML comment, just skip it.
    if (isBlockInsideCommentRegion(curBlock)) {
        setCurrentBlockState(HighlightBlockState::Comment);
        goto exit;
    }

    // PEG Markdown Highlight does not handle the ``` code block correctly.
    setCurrentBlockState(HighlightBlockState::Normal);
    highlightCodeBlock(curBlock, text);

    if (currentBlockState() == HighlightBlockState::Normal) {
        if (isVerbatimBlock(curBlock)) {
            setCurrentBlockState(HighlightBlockState::Verbatim);
            goto exit;
        }

        highlightHeaderFast(blockNum, text);

        if (m_enableMathjax
            && currentBlockState() == HighlightBlockState::Normal) {
            highlightMathJax(curBlock, text);
        }
    }

    // PEG Markdown Highlight does not handle links with spaces in the URL.
    // Links in the URL should be encoded to %20. We just let it be here and won't
    // fix this.
    // highlightLinkWithSpacesInURL(text);

    if (currentBlockState() != HighlightBlockState::CodeBlock) {
        goto exit;
    }

    // Highlight CodeBlock using VCodeBlockHighlightHelper.
    if (m_codeBlockHighlights.size() > blockNum) {
        const QVector<HLUnitStyle> &units = m_codeBlockHighlights[blockNum];
        if (!units.isEmpty()) {
            QVector<QTextCharFormat *> formats(units.size(), NULL);
            for (int i = 0; i < units.size(); ++i) {
                const HLUnitStyle &unit = units[i];
                auto it = m_codeBlockStyles.find(unit.style);
                if (it == m_codeBlockStyles.end()) {
                    continue;
                }

                formats[i] = &(*it);

                QTextCharFormat newFormat = m_codeBlockFormat;
                newFormat.merge(*it);
                for (int j = i - 1; j >= 0; --j) {
                    if (units[j].start + units[j].length <= unit.start) {
                        // It won't affect current unit.
                        continue;
                    } else {
                        // Merge the format.
                        if (formats[j]) {
                            QTextCharFormat tmpFormat(newFormat);
                            newFormat = *(formats[j]);
                            // tmpFormat takes precedence.
                            newFormat.merge(tmpFormat);
                        }
                    }
                }

                setFormat(unit.start, unit.length, newFormat);
            }
        }
    }

    highlightCodeBlockColorColumn(text);

exit:
    highlightChanged();
}

static bool compHLUnit(const HLUnit &p_a, const HLUnit &p_b)
{
    if (p_a.start < p_b.start) {
        return true;
    } else if (p_a.start == p_b.start) {
        return p_a.length > p_b.length;
    } else {
        return false;
    }
}

void HGMarkdownHighlighter::initBlockHighlightFromResult(int nrBlocks)
{
    m_blockHighlights.resize(nrBlocks);
    for (int i = 0; i < m_blockHighlights.size(); ++i) {
        m_blockHighlights[i].clear();
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
            // elem_cursor->pos and elem_cursor->end is the start
            // and end position of the element in document.
            if (elem_cursor->end <= elem_cursor->pos) {
                elem_cursor = elem_cursor->next;
                continue;
            }

            initBlockHighlihgtOne(elem_cursor->pos, elem_cursor->end, i);
            elem_cursor = elem_cursor->next;
        }
    }

    // Sort m_blockHighlights.
    for (int i = 0; i < m_blockHighlights.size(); ++i) {
        if (m_blockHighlights[i].size() > 1) {
            std::sort(m_blockHighlights[i].begin(), m_blockHighlights[i].end(), compHLUnit);
        }
    }
}

void HGMarkdownHighlighter::initHtmlCommentRegionsFromResult()
{
    m_commentRegions.clear();

    if (!result) {
        return;
    }

    pmh_element *elem = result[pmh_COMMENT];
    while (elem != NULL) {
        if (elem->end <= elem->pos) {
            elem = elem->next;
            continue;
        }

        initBlockElementRegionOne(m_commentRegions, elem->pos, elem->end);
        elem = elem->next;
    }
}

void HGMarkdownHighlighter::initImageRegionsFromResult()
{
    // From Qt5.7, the capacity is preserved.
    m_imageRegions.clear();

    if (!result) {
        return;
    }

    pmh_element *elem = result[pmh_IMAGE];
    while (elem != NULL) {
        if (elem->end <= elem->pos) {
            elem = elem->next;
            continue;
        }

        m_imageRegions.push_back(VElementRegion(elem->pos, elem->end));
        elem = elem->next;
    }
}

void HGMarkdownHighlighter::initVerbatimBlocksFromResult()
{
    m_verbatimBlocks.clear();
    if (!result) {
        return;
    }

    pmh_element *elem = result[pmh_VERBATIM];
    while (elem != NULL) {
        if (elem->end <= elem->pos) {
            elem = elem->next;
            continue;
        }

        // [firstBlock, lastBlock].
        int firstBlock = document->findBlock(elem->pos).blockNumber();
        int lastBlock = document->findBlock(elem->end - 1).blockNumber();
        for (int i = firstBlock; i <= lastBlock; ++i) {
            m_verbatimBlocks.insert(i);
        }

        elem = elem->next;
    }
}

void HGMarkdownHighlighter::initHeaderRegionsFromResult()
{
    // From Qt5.7, the capacity is preserved.
    m_headerBlocks.clear();
    m_headerRegions.clear();

    if (!result) {
        return;
    }

    pmh_element_type hx[6] = {pmh_H1, pmh_H2, pmh_H3, pmh_H4, pmh_H5, pmh_H6};
    for (int i = 0; i < 6; ++i) {
        pmh_element *elem = result[hx[i]];
        while (elem != NULL) {
            if (elem->end <= elem->pos) {
                elem = elem->next;
                continue;
            }

            m_headerRegions.push_back(VElementRegion(elem->pos, elem->end));

            QTextBlock block = document->findBlock(elem->pos);
            if (block.isValid()) {
                // Header element will contain the new line character.
                m_headerBlocks.insert(block.blockNumber(), HeaderBlockInfo(i, elem->end - elem->pos - 1));
            }

            elem = elem->next;
        }
    }

    std::sort(m_headerRegions.begin(), m_headerRegions.end());
}

void HGMarkdownHighlighter::initBlockHighlihgtOne(unsigned long pos,
                                                  unsigned long end,
                                                  int styleIndex)
{
    // When the the highlight element is at the end of document, @end will equals
    // to the characterCount.
    unsigned int nrChar = (unsigned int)document->characterCount();
    if (end >= nrChar && nrChar > 0) {
        end = nrChar - 1;
    }

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

        m_blockHighlights[i].append(unit);
    }
}

void HGMarkdownHighlighter::highlightCodeBlock(const QTextBlock &p_block, const QString &p_text)
{
    VTextBlockData *blockData = currentBlockData();
    Q_ASSERT(blockData);

    int length = 0;
    int index = -1;
    int preState = previousBlockState();
    int state = HighlightBlockState::Normal;

    if (preState != HighlightBlockState::CodeBlock
        && preState != HighlightBlockState::CodeBlockStart) {
        // Need to find a new code block start.
        index = codeBlockStartExp.indexIn(p_text);
        if (index >= 0 && !isVerbatimBlock(p_block)) {
            // Start a new code block.
            length = p_text.length();
            state = HighlightBlockState::CodeBlockStart;

            // The leading spaces of code block start and end must be identical.
            int startLeadingSpaces = codeBlockStartExp.capturedTexts()[1].size();
            blockData->setCodeBlockIndentation(startLeadingSpaces);
        } else {
            // A normal block.
            blockData->setCodeBlockIndentation(-1);
            return;
        }
    } else {
        // Need to find a code block end.
        int startLeadingSpaces = 0;
        VTextBlockData *preBlockData = previousBlockData();
        if (preBlockData) {
            startLeadingSpaces = preBlockData->getCodeBlockIndentation();
        }

        index = codeBlockEndExp.indexIn(p_text);

        // The closing ``` should have the same indentation as the open ```.
        if (index >= 0
            && startLeadingSpaces == codeBlockEndExp.capturedTexts()[1].size()) {
            // End of code block.
            length = p_text.length();
            state = HighlightBlockState::CodeBlockEnd;
        } else {
            // Within code block.
            index = 0;
            length = p_text.length();
            state = HighlightBlockState::CodeBlock;
        }

        blockData->setCodeBlockIndentation(startLeadingSpaces);
    }

    setCurrentBlockState(state);
    setFormat(index, length, m_codeBlockFormat);
}

static bool intersect(const QList<QPair<int, int>> &p_indices, int &p_start, int &p_end)
{
    for (auto const & range : p_indices) {
        if (p_end <= range.first) {
            return false;
        } else if (p_start < range.second) {
            return true;
        }
    }

    return false;
}

void HGMarkdownHighlighter::highlightMathJax(const QTextBlock &p_block, const QString &p_text)
{
    const int blockMarkLength = 2;
    const int inlineMarkLength = 1;

    VTextBlockData *blockData = currentBlockData();
    Q_ASSERT(blockData);

    int blockNumber = p_block.blockNumber();

    int startIdx = 0;
    // Next position to search.
    int pos = 0;
    HighlightBlockState state = (HighlightBlockState)previousBlockState();

    QList<QPair<int, int>> blockIdices;

    bool fromPreBlock = true;
    // Mathjax block formula.
    if (state != HighlightBlockState::MathjaxBlock) {
        fromPreBlock = false;
        startIdx = findMathjaxMarker(blockNumber, p_text, pos, m_mathjaxBlockExp, blockMarkLength);
        pos = startIdx + blockMarkLength;
    }

    while (startIdx >= 0) {
        int endIdx = findMathjaxMarker(blockNumber, p_text, pos, m_mathjaxBlockExp, blockMarkLength);
        int mathLength = 0;
        MathjaxInfo info;
        bool valid = false;
        if (endIdx == -1) {
            mathLength = p_text.length() - startIdx;
            pos = p_text.length();

            if (isValidMathjaxRegion(blockNumber, startIdx, pos)) {
                valid = true;
                setCurrentBlockState(HighlightBlockState::MathjaxBlock);
                info.m_previewedAsBlock = false;
                info.m_index = startIdx,
                info.m_length = mathLength;
                if (fromPreBlock) {
                    VTextBlockData *preBlockData = previousBlockData();
                    Q_ASSERT(preBlockData);
                    const MathjaxInfo &preInfo = preBlockData->getPendingMathjax();
                    info.m_text = preInfo.text() + "\n" + p_text.mid(startIdx, mathLength);
                } else {
                    info.m_text = p_text.mid(startIdx, mathLength);
                }

                blockData->setPendingMathjax(info);
            }
        } else {
            // Found end marker of a formula.
            pos = endIdx + blockMarkLength;
            mathLength = pos - startIdx;

            if (isValidMathjaxRegion(blockNumber, startIdx, pos)) {
                valid = true;
                info.m_previewedAsBlock = false;
                info.m_index = startIdx;
                info.m_length = mathLength;
                if (fromPreBlock) {
                    // A cross-block formula.
                    if (pos >= p_text.length()) {
                        info.m_previewedAsBlock = true;
                    }

                    VTextBlockData *preBlockData = previousBlockData();
                    Q_ASSERT(preBlockData);
                    const MathjaxInfo &preInfo = preBlockData->getPendingMathjax();
                    info.m_text = preInfo.text() + "\n" + p_text.mid(startIdx, mathLength);
                } else {
                    // A formula within one block.
                    if (pos >= p_text.length() && startIdx == 0) {
                        info.m_previewedAsBlock = true;
                    }

                    info.m_text = p_text.mid(startIdx, mathLength);
                }

                blockData->addMathjax(info);
            }
        }

        fromPreBlock = false;

        if (valid) {
            blockIdices.append(QPair<int, int>(startIdx, pos));
            setFormat(startIdx, mathLength, m_mathjaxFormat);
            startIdx = findMathjaxMarker(blockNumber, p_text, pos, m_mathjaxBlockExp, blockMarkLength);
            pos = startIdx + blockMarkLength;
        } else {
            // Make the second mark as the first one and try again.
            if (endIdx == -1) {
                break;
            }

            startIdx = pos - blockMarkLength;
        }
    }

    // Mathjax inline formula.
    startIdx = 0;
    pos = 0;
    fromPreBlock = true;
    if (state != HighlightBlockState::MathjaxInline) {
        fromPreBlock = false;
        startIdx = findMathjaxMarker(blockNumber, p_text, pos, m_mathjaxInlineExp, inlineMarkLength);
        pos = startIdx + inlineMarkLength;
    }

    while (startIdx >= 0) {
        int endIdx = findMathjaxMarker(blockNumber, p_text, pos, m_mathjaxInlineExp, inlineMarkLength);
        int mathLength = 0;
        bool valid = false;
        if (endIdx == -1) {
            mathLength = p_text.length() - startIdx;
            pos = p_text.length();
        } else {
            pos = endIdx + inlineMarkLength;
            mathLength = pos - startIdx;
        }

        valid = isValidMathjaxRegion(blockNumber, startIdx, pos);
        // Check if it intersect with blocks.
        if (valid && !intersect(blockIdices, startIdx, pos)) {
            // A valid inline mathjax.
            MathjaxInfo info;
            if (endIdx == -1) {
                setCurrentBlockState(HighlightBlockState::MathjaxInline);

                info.m_previewedAsBlock = false;
                info.m_index = startIdx,
                info.m_length = mathLength;
                if (fromPreBlock) {
                    VTextBlockData *preBlockData = previousBlockData();
                    Q_ASSERT(preBlockData);
                    const MathjaxInfo &preInfo = preBlockData->getPendingMathjax();
                    info.m_text = preInfo.text() + "\n" + p_text.mid(startIdx, mathLength);
                } else {
                    info.m_text = p_text.mid(startIdx, mathLength);
                }

                blockData->setPendingMathjax(info);
            } else {
                info.m_previewedAsBlock = false;
                info.m_index = startIdx;
                info.m_length = mathLength;
                if (fromPreBlock) {
                    // A cross-block formula.
                    if (pos >= p_text.length()) {
                        info.m_previewedAsBlock = true;
                    }

                    VTextBlockData *preBlockData = previousBlockData();
                    Q_ASSERT(preBlockData);
                    const MathjaxInfo &preInfo = preBlockData->getPendingMathjax();
                    info.m_text = preInfo.text() + "\n" + p_text.mid(startIdx, mathLength);
                } else {
                    // A formula within one block.
                    if (pos >= p_text.length() && startIdx == 0) {
                        info.m_previewedAsBlock = true;
                    }

                    info.m_text = p_text.mid(startIdx, mathLength);
                }

                blockData->addMathjax(info);
            }

            setFormat(startIdx, mathLength, m_mathjaxFormat);

            startIdx = m_mathjaxInlineExp.indexIn(p_text, pos);
            pos = startIdx + m_mathjaxInlineExp.matchedLength();
            startIdx = pos - inlineMarkLength;
        } else {
            // Make the second mark as the first one and try again.
            if (endIdx == -1) {
                break;
            }

            startIdx = pos - inlineMarkLength;
        }

        fromPreBlock = false;
    }
}

void HGMarkdownHighlighter::highlightCodeBlockColorColumn(const QString &p_text)
{
    int cc = g_config->getColorColumn();
    if (cc <= 0 || currentBlockState() != HighlightBlockState::CodeBlock) {
        return;
    }

    VTextBlockData *blockData = currentBlockData();
    Q_ASSERT(blockData);
    int indent = blockData->getCodeBlockIndentation();
    if (indent == -1) {
        return;
    }

    cc += indent;
    if (p_text.size() < cc) {
        return;
    }

    setFormat(cc - 1, 1, m_colorColumnFormat);
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

    if (highlightingStyles.isEmpty()) {
        goto exit;
    }

    {
    m_blockHLResultReady = false;

    int nrBlocks = document->blockCount();
    parseInternal();

    initBlockHighlightFromResult(nrBlocks);

    m_blockHLResultReady = true;

    initHtmlCommentRegionsFromResult();

    initImageRegionsFromResult();

    initHeaderRegionsFromResult();

    initVerbatimBlocksFromResult();

    initInlineCodeRegionsFromResult();

    initBoldItalicRegionsFromResult();

    initLinkRegionsFromResult();

    if (result) {
        pmh_free_elements(result);
        result = NULL;
    }
    }

exit:
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

    pmh_markdown_to_elements(content, pmh_EXT_STRIKE, &result);
}

void HGMarkdownHighlighter::handleContentChange(int /* position */, int charsRemoved, int charsAdded)
{
    if (charsRemoved == 0 && charsAdded == 0) {
        return;
    }

    m_signalOut = false;

    timer->stop();
    timer->start();
}

void HGMarkdownHighlighter::startParseAndHighlight(bool p_fast)
{
    qDebug() << "HGMarkdownHighlighter start a new parse (fast" << p_fast << ")";
    parse();

    m_signalOut = !p_fast;

    if (!p_fast) {
        updateCodeBlocks();
    }

    rehighlight();
}

void HGMarkdownHighlighter::updateHighlight()
{
    timer->stop();
    startParseAndHighlight(false);
}

void HGMarkdownHighlighter::updateHighlightFast()
{
    timer->stop();
    startParseAndHighlight(true);
}

bool HGMarkdownHighlighter::updateCodeBlocks()
{
    if (!g_config->getEnableCodeBlockHighlight()) {
        m_codeBlockHighlights.clear();
        return false;
    }

    m_codeBlockHighlights.resize(document->blockCount());
    for (int i = 0; i < m_codeBlockHighlights.size(); ++i) {
        m_codeBlockHighlights[i].clear();
    }

    QVector<VCodeBlock> codeBlocks;

    VCodeBlock item;
    bool inBlock = false;
    int startLeadingSpaces = -1;

    // Only handle complete codeblocks.
    QTextBlock block = document->firstBlock();
    while (block.isValid()) {
        if (!inBlock && isVerbatimBlock(block)) {
            block = block.next();
            continue;
        }

        QString text = block.text();
        if (inBlock) {
            item.m_text = item.m_text + "\n" + text;
            int idx = codeBlockEndExp.indexIn(text);
            if (idx >= 0 && codeBlockEndExp.capturedTexts()[1].size() == startLeadingSpaces) {
                // End block.
                inBlock = false;
                item.m_endBlock = block.blockNumber();

                // See if it is a code block inside HTML comment.
                if (!isBlockInsideCommentRegion(block)) {
                    qDebug() << "add one code block in lang" << item.m_lang;
                    codeBlocks.append(item);
                }
            }
        } else {
            int idx = codeBlockStartExp.indexIn(text);
            if (idx >= 0) {
                // Start block.
                inBlock = true;
                item.m_startBlock = block.blockNumber();
                item.m_startPos = block.position();
                item.m_text = text;
                if (codeBlockStartExp.captureCount() == 2) {
                    item.m_lang = codeBlockStartExp.capturedTexts()[2];
                }

                startLeadingSpaces = codeBlockStartExp.capturedTexts()[1].size();
            }
        }

        block = block.next();
    }

    m_numOfCodeBlockHighlightsToRecv = codeBlocks.size();
    emit codeBlocksUpdated(codeBlocks);
    return m_numOfCodeBlockHighlightsToRecv > 0;
}

static bool compHLUnitStyle(const HLUnitStyle &a, const HLUnitStyle &b)
{
    if (a.start < b.start) {
        return true;
    } else if (a.start == b.start) {
        return a.length > b.length;
    } else {
        return false;
    }
}

void HGMarkdownHighlighter::setCodeBlockHighlights(const QVector<HLUnitPos> &p_units)
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

        // Text has been changed. Abandon the obsolete parsed result.
        if (startBlockNum == -1 || endBlockNum >= highlights.size()) {
            goto exit;
        }

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
            if (units.size() > 1) {
                std::sort(units.begin(), units.end(), compHLUnitStyle);
            }

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

bool HGMarkdownHighlighter::isBlockInsideCommentRegion(const QTextBlock &p_block) const
{
    if (!p_block.isValid()) {
        return false;
    }

    auto it = m_commentRegions.find(p_block.blockNumber());
    if (it != m_commentRegions.end()) {
        const QVector<VElementRegion> &regs = it.value();
        if (regs.size() == 1
            && regs[0].m_startPos == 0
            && regs[0].m_endPos == p_block.length()) {
            return true;
        }
    }

    return false;
}

void HGMarkdownHighlighter::highlightChanged()
{
    m_completeTimer->stop();
    m_completeTimer->start();
}

bool HGMarkdownHighlighter::isValidHeader(const QString &p_text)
{
    // There must exist spaces after #s.
    // No more than 6 #s.
    int nrNumberSign = 0;
    for (int i = 0; i < p_text.size(); ++i) {
        QChar ch = p_text[i];
        if (ch.isSpace()) {
            return nrNumberSign > 0;
        } else if (ch == QChar('#')) {
            if (++nrNumberSign > 6) {
                return false;
            }
        } else {
            return false;
        }
    }

    return false;
}

void HGMarkdownHighlighter::highlightHeaderFast(int p_blockNumber, const QString &p_text)
{
    auto it = m_headerBlocks.find(p_blockNumber);
    if (it != m_headerBlocks.end()) {
        const HeaderBlockInfo &info = it.value();
        if (!isValidHeader(p_text)) {
            // Set an empty format to clear formats. It seems to work.
            setFormat(0, p_text.size(), QTextCharFormat());
        } else {
            if (info.m_length < p_text.size()) {
                setFormat(info.m_length,
                          p_text.size() - info.m_length,
                          m_headerStyles[info.m_level]);
            }

            setCurrentBlockState(HighlightBlockState::Header);
        }
    }
}

void HGMarkdownHighlighter::updateMathjaxBlocks()
{
    if (!m_enableMathjax) {
        return;
    }

    QVector<VMathjaxBlock> blocks;
    QTextBlock bl = document->firstBlock();
    while (bl.isValid()) {
        VTextBlockData *data = static_cast<VTextBlockData *>(bl.userData());
        if (!data) {
            bl = bl.next();
            continue;
        }

        const QVector<MathjaxInfo> &info = data->getMathjax();
        for (auto const & it : info) {
            blocks.append(VMathjaxBlock(bl.blockNumber(), it));
        }

        bl = bl.next();
    }

    emit mathjaxBlocksUpdated(blocks);
}

void HGMarkdownHighlighter::initInlineCodeRegionsFromResult()
{
    m_inlineCodeRegions.clear();

    if (!result) {
        return;
    }

    pmh_element *elem = result[pmh_CODE];
    while (elem != NULL) {
        if (elem->end <= elem->pos) {
            elem = elem->next;
            continue;
        }

        initBlockElementRegionOne(m_inlineCodeRegions, elem->pos, elem->end);
        elem = elem->next;
    }
}

void HGMarkdownHighlighter::initBoldItalicRegionsFromResult()
{
    m_boldItalicRegions.clear();

    if (!result) {
        return;
    }

    pmh_element_type types[2] = {pmh_EMPH, pmh_STRONG};

    for (int i = 0; i < 2; ++i) {
        pmh_element *elem = result[types[i]];
        while (elem != NULL) {
            if (elem->end <= elem->pos) {
                elem = elem->next;
                continue;
            }

            initBlockElementRegionOne(m_boldItalicRegions, elem->pos, elem->end);
            elem = elem->next;
        }
    }
}

void HGMarkdownHighlighter::initLinkRegionsFromResult()
{
    m_linkRegions.clear();

    if (!result) {
        return;
    }

    pmh_element_type types[2] = {pmh_LINK, pmh_IMAGE};

    for (int i = 0; i < 2; ++i) {
        pmh_element *elem = result[types[i]];
        while (elem != NULL) {
            if (elem->end <= elem->pos) {
                elem = elem->next;
                continue;
            }

            initBlockElementRegionOne(m_linkRegions, elem->pos, elem->end);
            elem = elem->next;
        }
    }
}

void HGMarkdownHighlighter::initBlockElementRegionOne(QHash<int, QVector<VElementRegion>> &p_regs,
                                                      unsigned long p_pos,
                                                      unsigned long p_end)
{
    // When the the highlight element is at the end of document, @p_end will equals
    // to the characterCount.
    unsigned int nrChar = (unsigned int)document->characterCount();
    if (p_end >= nrChar && nrChar > 0) {
        p_end = nrChar - 1;
    }

    int startBlockNum = document->findBlock(p_pos).blockNumber();
    int endBlockNum = document->findBlock(p_end).blockNumber();

    for (int i = startBlockNum; i <= endBlockNum; ++i)
    {
        QTextBlock block = document->findBlockByNumber(i);
        int blockStartPos = block.position();

        QVector<VElementRegion> &regs = p_regs[i];
        int start, end;
        if (i == startBlockNum) {
            start = p_pos - blockStartPos;
            end = (startBlockNum == endBlockNum) ? (p_end - blockStartPos)
                                                 : block.length();
        } else if (i == endBlockNum) {
            start = 0;
            end = p_end - blockStartPos;
        } else {
            start = 0;
            end = block.length();
        }

        regs.append(VElementRegion(start, end));
    }
}

static bool indexInsideRegion(const QVector<VElementRegion> &p_regions, int p_idx)
{
    for (auto const & reg : p_regions) {
        if (reg.contains(p_idx)) {
            return true;
        }
    }

    return false;
}

int HGMarkdownHighlighter::findMathjaxMarker(int p_blockNumber,
                                             const QString &p_text,
                                             int p_pos,
                                             QRegExp &p_reg,
                                             int p_markerLength)
{
    if (p_pos >= p_text.size()) {
        return -1;
    }

    int idx = -1;
    auto inlineCodeIt = m_inlineCodeRegions.find(p_blockNumber);
    auto commentIt = m_commentRegions.find(p_blockNumber);
    auto boldItalicIt = m_boldItalicRegions.find(p_blockNumber);
    auto linkIt = m_linkRegions.find(p_blockNumber);

    while (p_pos < p_text.size()) {
        idx = p_reg.indexIn(p_text, p_pos);
        if (idx == -1) {
            return idx;
        }

        p_pos = idx + p_reg.matchedLength();
        idx = p_pos - p_markerLength;

        // Check if this idx is legal.
        // Check inline code.
        if (inlineCodeIt != m_inlineCodeRegions.end()) {
            if (indexInsideRegion(inlineCodeIt.value(), idx)) {
                idx = -1;
                continue;
            }
        }

        if (commentIt != m_commentRegions.end()) {
            if (indexInsideRegion(commentIt.value(), idx)) {
                idx = -1;
                continue;
            }
        }

        if (boldItalicIt != m_boldItalicRegions.end()) {
            if (indexInsideRegion(boldItalicIt.value(), idx)) {
                idx = -1;
                continue;
            }
        }

        if (linkIt != m_linkRegions.end()) {
            if (indexInsideRegion(linkIt.value(), idx)) {
                idx = -1;
                continue;
            }
        }

        break;
    }

    return idx;
}

static bool intersectRegion(const QVector<VElementRegion> &p_regions,
                            int p_start,
                            int p_end)
{
    for (auto const & reg : p_regions) {
        if (reg.intersect(p_start, p_end)) {
            return true;
        }
    }

    return false;
}

bool HGMarkdownHighlighter::isValidMathjaxRegion(int p_blockNumber,
                                                 int p_start,
                                                 int p_end)
{
    auto inlineCodeIt = m_inlineCodeRegions.find(p_blockNumber);
    if (inlineCodeIt != m_inlineCodeRegions.end()) {
        if (intersectRegion(inlineCodeIt.value(), p_start, p_end)) {
            return false;
        }
    }

    auto commentIt = m_commentRegions.find(p_blockNumber);
    if (commentIt != m_commentRegions.end()) {
        if (intersectRegion(commentIt.value(), p_start, p_end)) {
            return false;
        }
    }

    auto boldItalicIt = m_boldItalicRegions.find(p_blockNumber);
    if (boldItalicIt != m_boldItalicRegions.end()) {
        if (intersectRegion(boldItalicIt.value(), p_start, p_end)) {
            return false;
        }
    }

    auto linkIt = m_linkRegions.find(p_blockNumber);
    if (linkIt != m_linkRegions.end()) {
        if (intersectRegion(linkIt.value(), p_start, p_end)) {
            return false;
        }
    }

    return true;
}

void HGMarkdownHighlighter::completeHighlight()
{
    if (m_signalOut) {
        m_signalOut = false;
        updateMathjaxBlocks();
        emit imageLinksUpdated(m_imageRegions);
        emit headersUpdated(m_headerRegions);
    }

    emit highlightCompleted();
}
