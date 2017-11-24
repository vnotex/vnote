#include <QtGui>
#include <QtDebug>
#include <QTextCursor>
#include <algorithm>
#include "hgmarkdownhighlighter.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "vtextblockdata.h"

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
    : QSyntaxHighlighter(parent), highlightingStyles(styles),
      m_codeBlockStyles(codeBlockStyles), m_numOfCodeBlockHighlightsToRecv(0),
      parsing(0), waitInterval(waitInterval), content(NULL), capacity(0), result(NULL)
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

    resizeBuffer(initCapacity);
    document = parent;

    timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(this->waitInterval);
    connect(timer, &QTimer::timeout, this, &HGMarkdownHighlighter::timerTimeout);

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
    VTextBlockData *blockData = dynamic_cast<VTextBlockData *>(currentBlockUserData());
    if (!blockData) {
        blockData = new VTextBlockData();
        setCurrentBlockUserData(blockData);
    }

    bool contains = p_text.contains(QChar::ObjectReplacementCharacter);
    blockData->setContainsPreviewImage(contains);

    auto curIt = m_potentialPreviewBlocks.find(p_blockNum);
    if (curIt == m_potentialPreviewBlocks.end()) {
        if (contains) {
            m_potentialPreviewBlocks.insert(p_blockNum, true);
        }
    } else {
        if (!contains) {
            m_potentialPreviewBlocks.erase(curIt);
        }
    }

    // Delete elements beyond current block count.
    int blocks = document->blockCount();
    if (!m_potentialPreviewBlocks.isEmpty()) {
        auto it = m_potentialPreviewBlocks.end();
        do {
            --it;
            if (it.key() >= blocks) {
                it = m_potentialPreviewBlocks.erase(it);
            } else {
                break;
            }
        } while (it != m_potentialPreviewBlocks.begin());
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

    highlightCodeBlockColorColumn(text);

exit:
    highlightChanged();
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
    if (!result) {
        // From Qt5.7, the capacity is preserved.
        m_imageRegions.clear();
        emit imageLinksUpdated(m_imageRegions);
        return;
    }

    int idx = 0;
    int oriSize = m_imageRegions.size();
    pmh_element *elem = result[pmh_IMAGE];
    while (elem != NULL) {
        if (elem->end <= elem->pos) {
            elem = elem->next;
            continue;
        }

        if (idx < oriSize) {
            // Try to reuse the original element.
            VElementRegion &reg = m_imageRegions[idx];
            if ((int)elem->pos != reg.m_startPos || (int)elem->end != reg.m_endPos) {
                reg.m_startPos = (int)elem->pos;
                reg.m_endPos = (int)elem->end;
            }
        } else {
            m_imageRegions.push_back(VElementRegion(elem->pos, elem->end));
        }

        ++idx;
        elem = elem->next;
    }

    if (idx < oriSize) {
        m_imageRegions.resize(idx);
    }

    qDebug() << "highlighter: parse" << m_imageRegions.size() << "image regions";

    emit imageLinksUpdated(m_imageRegions);
}

void HGMarkdownHighlighter::initHeaderRegionsFromResult()
{
    if (!result) {
        // From Qt5.7, the capacity is preserved.
        m_headerRegions.clear();
        emit headersUpdated(m_headerRegions);
        return;
    }

    int idx = 0;
    int oriSize = m_headerRegions.size();
    pmh_element_type hx[6] = {pmh_H1, pmh_H2, pmh_H3, pmh_H4, pmh_H5, pmh_H6};
    for (int i = 0; i < 6; ++i) {
        pmh_element *elem = result[hx[i]];
        while (elem != NULL) {
            if (elem->end <= elem->pos
                || !isValidHeader(elem->pos, elem->end)) {
                elem = elem->next;
                continue;
            }

            if (idx < oriSize) {
                // Try to reuse the original element.
                VElementRegion &reg = m_headerRegions[idx];
                if ((int)elem->pos != reg.m_startPos || (int)elem->end != reg.m_endPos) {
                    reg.m_startPos = (int)elem->pos;
                    reg.m_endPos = (int)elem->end;
                }
            } else {
                m_headerRegions.push_back(VElementRegion(elem->pos, elem->end));
            }

            ++idx;
            elem = elem->next;
        }
    }

    if (idx < oriSize) {
        m_headerRegions.resize(idx);
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

        blockHighlights[i].append(unit);
    }
}

void HGMarkdownHighlighter::highlightCodeBlock(const QString &text)
{
    static int startLeadingSpaces = -1;
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
            startLeadingSpaces = codeBlockStartExp.capturedTexts()[1].size();
        } else {
            // A normal block.
            startLeadingSpaces = -1;
            return;
        }
    } else {
        // Need to find a code block end.
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
    int nrBlocks = document->blockCount();
    parseInternal();

    initBlockHighlightFromResult(nrBlocks);

    initHtmlCommentRegionsFromResult();

    initImageRegionsFromResult();

    initHeaderRegionsFromResult();

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

void HGMarkdownHighlighter::timerTimeout()
{
    qDebug() << "HGMarkdownHighlighter start a new parse";
    parse();
    if (!updateCodeBlocks()) {
        rehighlight();
    }

    highlightChanged();
}

void HGMarkdownHighlighter::updateHighlight()
{
    timer->stop();
    timerTimeout();
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
    for (unsigned long i = p_pos; i < p_end; ++i) {
        QChar ch = document->characterAt(i);
        if (ch.isSpace()) {
            return true;
        } else if (ch != QChar('#')) {
            return false;
        }
    }

    return false;
}
