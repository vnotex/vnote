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
      content(NULL),
      capacity(0),
      result(NULL)
{
    codeBlockStartExp = QRegExp(VUtils::c_fencedCodeBlockStartRegExp);
    codeBlockEndExp = QRegExp(VUtils::c_fencedCodeBlockEndRegExp);

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

    m_colorColumnFormat = codeBlockFormat;
    m_colorColumnFormat.setForeground(QColor(g_config->getEditorColorColumnFg()));
    m_colorColumnFormat.setBackground(QColor(g_config->getEditorColorColumnBg()));

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

    static const int completeWaitTime = 500;
    m_completeTimer = new QTimer(this);
    m_completeTimer->setSingleShot(true);
    m_completeTimer->setInterval(completeWaitTime);
    connect(m_completeTimer, &QTimer::timeout,
            this, &HGMarkdownHighlighter::highlightCompleted);

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
    }

    if (blockData->getPreviews().isEmpty()) {
        m_possiblePreviewBlocks.remove(p_blockNum);
    } else {
        m_possiblePreviewBlocks.insert(p_blockNum);
    }

    blockData->setCodeBlockIndentation(-1);
}

void HGMarkdownHighlighter::highlightBlock(const QString &text)
{
    int blockNum = currentBlock().blockNumber();
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
    if (isBlockInsideCommentRegion(currentBlock())) {
        setCurrentBlockState(HighlightBlockState::Comment);
        goto exit;
    }

    // PEG Markdown Highlight does not handle the ``` code block correctly.
    setCurrentBlockState(HighlightBlockState::Normal);
    highlightCodeBlock(text);

    // PEG Markdown Highlight does not handle links with spaces in the URL.
    // Links in the URL should be encoded to %20. We just let it be here and won't
    // fix this.
    // highlightLinkWithSpacesInURL(text);

    highlightHeaderFast(blockNum, text);

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

                if (i == 0) {
                    // No need to merge format.
                    setFormat(unit.start, unit.length, *it);
                } else {
                    QTextCharFormat newFormat = *it;
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

        // pmh_H1 to pmh_H6 is continuous.
        bool isHeader = style.type >= pmh_H1 && style.type <= pmh_H6;

        while (elem_cursor != NULL)
        {
            // elem_cursor->pos and elem_cursor->end is the start
            // and end position of the element in document.
            if (elem_cursor->end <= elem_cursor->pos) {
                elem_cursor = elem_cursor->next;
                continue;
            }

            // Check header. Skip those headers with no spaces after #s.
            if (isHeader
                && !isValidHeader(elem_cursor->pos, elem_cursor->end)) {
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
    // From Qt5.7, the capacity is preserved.
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

        m_commentRegions.push_back(VElementRegion(elem->pos, elem->end));

        elem = elem->next;
    }

    qDebug() << "highlighter: parse" << m_commentRegions.size() << "HTML comment regions";
}

void HGMarkdownHighlighter::initImageRegionsFromResult()
{
    // From Qt5.7, the capacity is preserved.
    m_imageRegions.clear();

    if (!result) {
        emit imageLinksUpdated(m_imageRegions);
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

    qDebug() << "highlighter: parse" << m_imageRegions.size() << "image regions";

    emit imageLinksUpdated(m_imageRegions);
}

void HGMarkdownHighlighter::initHeaderRegionsFromResult()
{
    // From Qt5.7, the capacity is preserved.
    m_headerBlocks.clear();
    m_headerRegions.clear();

    if (!result) {
        emit headersUpdated(m_headerRegions);
        return;
    }

    pmh_element_type hx[6] = {pmh_H1, pmh_H2, pmh_H3, pmh_H4, pmh_H5, pmh_H6};
    for (int i = 0; i < 6; ++i) {
        pmh_element *elem = result[hx[i]];
        while (elem != NULL) {
            if (elem->end <= elem->pos
                || !isValidHeader(elem->pos, elem->end)) {
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

    qDebug() << "highlighter: parse" << m_headerRegions.size() << "header regions";

    emit headersUpdated(m_headerRegions);
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

void HGMarkdownHighlighter::highlightCodeBlock(const QString &text)
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
        index = codeBlockStartExp.indexIn(text);
        if (index >= 0) {
            // Start a new code block.
            length = text.length();
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

        index = codeBlockEndExp.indexIn(text);

        // The closing ``` should have the same indentation as the open ```.
        if (index >= 0
            && startLeadingSpaces == codeBlockEndExp.capturedTexts()[1].size()) {
            // End of code block.
            length = text.length();
            state = HighlightBlockState::CodeBlockEnd;
        } else {
            // Within code block.
            index = 0;
            length = text.length();
            state = HighlightBlockState::CodeBlock;
        }

        blockData->setCodeBlockIndentation(startLeadingSpaces);
    }

    setCurrentBlockState(state);
    setFormat(index, length, codeBlockFormat);
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

void HGMarkdownHighlighter::parse(bool p_fast)
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

    if (!p_fast) {
        initHtmlCommentRegionsFromResult();

        initImageRegionsFromResult();

        initHeaderRegionsFromResult();
    }

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

void HGMarkdownHighlighter::startParseAndHighlight(bool p_fast)
{
    qDebug() << "HGMarkdownHighlighter start a new parse (fast" << p_fast << ")";
    parse(p_fast);

    if (p_fast) {
        rehighlight();
    } else {
        if (!updateCodeBlocks()) {
            rehighlight();
        }

        highlightChanged();
    }
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
    if (m_numOfCodeBlockHighlightsToRecv > 0) {
        emit codeBlocksUpdated(codeBlocks);
        return true;
    } else {
        return false;
    }
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

    int start = p_block.position();
    int end = start + p_block.length();

    for (auto const & reg : m_commentRegions) {
        if (reg.contains(start) && reg.contains(end)) {
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

bool HGMarkdownHighlighter::isValidHeader(unsigned long p_pos, unsigned long p_end)
{
    // There must exist spaces after #s.
    // No more than 6 #s.
    int nrNumberSign = 0;
    for (unsigned long i = p_pos; i < p_end; ++i) {
        QChar ch = document->characterAt(i);
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
    if (currentBlockState() != HighlightBlockState::Normal) {
        return;
    }

    auto it = m_headerBlocks.find(p_blockNumber);
    if (it != m_headerBlocks.end()) {
        const HeaderBlockInfo &info = it.value();
        if (!isValidHeader(p_text)) {
            // Set an empty format to clear formats. It seems to work.
            setFormat(0, p_text.size(), QTextCharFormat());
        } else if (info.m_length < p_text.size()) {
            setFormat(info.m_length, p_text.size() - info.m_length, m_headerStyles[info.m_level]);
        }
    }
}
