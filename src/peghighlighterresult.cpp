#include "peghighlighterresult.h"

#include <QTextDocument>
#include <QTextBlock>

#include "pegmarkdownhighlighter.h"
#include "utils/vutils.h"

PegHighlighterFastResult::PegHighlighterFastResult()
    : m_timeStamp(0)
{
}

PegHighlighterFastResult::PegHighlighterFastResult(const PegMarkdownHighlighter *p_peg,
                                                   const QSharedPointer<PegParseResult> &p_result)
    : m_timeStamp(p_result->m_timeStamp)
{
    PegHighlighterResult::parseBlocksHighlights(m_blocksHighlights, p_peg, p_result);
}


PegHighlighterResult::PegHighlighterResult()
    : m_timeStamp(0),
      m_numOfBlocks(0),
      m_codeBlockTimeStamp(0),
      m_numOfCodeBlockHighlightsToRecv(0)
{
    m_codeBlockStartExp = QRegularExpression(VUtils::c_fencedCodeBlockStartRegExp);
    m_codeBlockEndExp = QRegularExpression(VUtils::c_fencedCodeBlockEndRegExp);
}

PegHighlighterResult::PegHighlighterResult(const PegMarkdownHighlighter *p_peg,
                                           const QSharedPointer<PegParseResult> &p_result)
    : m_timeStamp(p_result->m_timeStamp),
      m_numOfBlocks(p_result->m_numOfBlocks),
      m_codeBlockHighlightReceived(false),
      m_codeBlockTimeStamp(0),
      m_numOfCodeBlockHighlightsToRecv(0)
{
    m_codeBlockStartExp = QRegularExpression(VUtils::c_fencedCodeBlockStartRegExp);
    m_codeBlockEndExp = QRegularExpression(VUtils::c_fencedCodeBlockEndRegExp);

    parseBlocksHighlights(m_blocksHighlights, p_peg, p_result);

    // Implicit sharing.
    m_imageRegions = p_result->m_imageRegions;
    m_headerRegions = p_result->m_headerRegions;

    parseFencedCodeBlocks(p_peg, p_result);

    parseMathjaxBlocks(p_peg, p_result);

    parseHRuleBlocks(p_peg, p_result);
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

void PegHighlighterResult::parseBlocksHighlights(QVector<QVector<HLUnit>> &p_blocksHighlights,
                                                 const PegMarkdownHighlighter *p_peg,
                                                 const QSharedPointer<PegParseResult> &p_result)
{
    p_blocksHighlights.resize(p_result->m_numOfBlocks);
    if (p_result->isEmpty()) {
        return;
    }

    int offset = p_result->m_offset;
    const QTextDocument *doc = p_peg->getDocument();
    const QVector<HighlightingStyle> &styles = p_peg->getStyles();
    auto pmhResult = p_result->m_pmhElements;
    for (int i = 0; i < styles.size(); i++)
    {
        const HighlightingStyle &style = styles[i];
        pmh_element *elem_cursor = pmhResult[style.type];
        while (elem_cursor != NULL)
        {
            // elem_cursor->pos and elem_cursor->end is the start
            // and end position of the element in document.
            if (elem_cursor->end <= elem_cursor->pos) {
                elem_cursor = elem_cursor->next;
                continue;
            }

            parseBlocksHighlightOne(p_blocksHighlights,
                                    doc,
                                    offset + elem_cursor->pos,
                                    offset + elem_cursor->end,
                                    i);
            elem_cursor = elem_cursor->next;
        }
    }

    // Sort p_blocksHighlights.
    for (int i = 0; i < p_blocksHighlights.size(); ++i) {
        if (p_blocksHighlights[i].size() > 1) {
            std::sort(p_blocksHighlights[i].begin(), p_blocksHighlights[i].end(), compHLUnit);
        }
    }
}

void PegHighlighterResult::parseBlocksHighlightOne(QVector<QVector<HLUnit>> &p_blocksHighlights,
                                                   const QTextDocument *p_doc,
                                                   unsigned long p_pos,
                                                   unsigned long p_end,
                                                   int p_styleIndex)
{
    // When the the highlight element is at the end of document, @p_end will equals
    // to the characterCount.
    unsigned int nrChar = (unsigned int)p_doc->characterCount();
    if (p_end >= nrChar && nrChar > 0) {
        p_end = nrChar - 1;
    }

    QTextBlock block = p_doc->findBlock(p_pos);
    int startBlockNum = block.blockNumber();
    int endBlockNum = p_doc->findBlock(p_end - 1).blockNumber();
    if (endBlockNum >= p_blocksHighlights.size()) {
        endBlockNum = p_blocksHighlights.size() - 1;
    }

    while (block.isValid())
    {
        int blockNum = block.blockNumber();
        if (blockNum > endBlockNum) {
            break;
        }

        int blockStartPos = block.position();
        HLUnit unit;
        if (blockNum == startBlockNum) {
            unit.start = p_pos - blockStartPos;
            unit.length = (startBlockNum == endBlockNum) ?
                          (p_end - p_pos) : (block.length() - unit.start);
        } else if (blockNum == endBlockNum) {
            unit.start = 0;
            unit.length = p_end - blockStartPos;
        } else {
            unit.start = 0;
            unit.length = block.length();
        }

        unit.styleIndex = p_styleIndex;

        Q_ASSERT(unit.length > 0);

        if (unit.length > 0) {
            p_blocksHighlights[blockNum].append(unit);
        }

        block = block.next();
    }
}

#if 0
void PegHighlighterResult::parseBlocksElementRegionOne(QHash<int, QVector<VElementRegion>> &p_regs,
                                                       const QTextDocument *p_doc,
                                                       unsigned long p_pos,
                                                       unsigned long p_end)
{
    // When the the highlight element is at the end of document, @p_end will equals
    // to the characterCount.
    unsigned int nrChar = (unsigned int)p_doc->characterCount();
    if (p_end >= nrChar && nrChar > 0) {
        p_end = nrChar - 1;
    }

    QTextBlock block = p_doc->findBlock(p_pos);
    int startBlockNum = block.blockNumber();
    int endBlockNum = p_doc->findBlock(p_end - 1).blockNumber();
    if (endBlockNum >= p_regs.size()) {
        endBlockNum = p_regs.size() - 1;
    }

    while (block.isValid())
    {
        int blockNum = block.blockNumber();
        if (blockNum > endBlockNum) {
            break;
        }

        int blockStartPos = block.position();
        QVector<VElementRegion> &regs = p_regs[blockNum];
        int start, end;
        if (blockNum == startBlockNum) {
            start = p_pos - blockStartPos;
            end = (startBlockNum == endBlockNum) ? (p_end - blockStartPos)
                                                 : block.length();
        } else if (blockNum == endBlockNum) {
            start = 0;
            end = p_end - blockStartPos;
        } else {
            start = 0;
            end = block.length();
        }

        regs.append(VElementRegion(start, end));
    }
}
#endif

void PegHighlighterResult::parseFencedCodeBlocks(const PegMarkdownHighlighter *p_peg,
                                                 const QSharedPointer<PegParseResult> &p_result)
{
    const QMap<int, VElementRegion> &regs = p_result->m_codeBlockRegions;

    const QTextDocument *doc = p_peg->getDocument();
    VCodeBlock item;
    bool inBlock = false;
    QString marker;
    for (auto it = regs.begin(); it != regs.end(); ++it) {
        QTextBlock block = doc->findBlock(it.value().m_startPos);
        int lastBlock = doc->findBlock(it.value().m_endPos - 1).blockNumber();
        if (lastBlock >= p_result->m_numOfBlocks) {
            lastBlock = p_result->m_numOfBlocks - 1;
        }

        while (block.isValid()) {
            int blockNumber = block.blockNumber();
            if (blockNumber > lastBlock) {
                break;
            }

            HighlightBlockState state = HighlightBlockState::Normal;
            QString text = block.text();
            if (inBlock) {
                item.m_text = item.m_text + "\n" + text;
                auto match = m_codeBlockEndExp.match(text);
                if (match.hasMatch() && marker == match.captured(2)) {
                    // End block.
                    inBlock = false;
                    marker.clear();

                    state = HighlightBlockState::CodeBlockEnd;
                    item.m_endBlock = blockNumber;
                    m_codeBlocks.append(item);
                } else {
                    // Within code block.
                    state = HighlightBlockState::CodeBlock;
                }
            } else {
                auto match = m_codeBlockStartExp.match(text);
                if (match.hasMatch()) {
                    // Start block.
                    inBlock = true;
                    marker = match.captured(2);

                    state = HighlightBlockState::CodeBlockStart;
                    item.m_startBlock = blockNumber;
                    item.m_startPos = block.position();
                    item.m_text = text;
                    item.m_lang = match.captured(3).trimmed();
                }
            }

            if (state != HighlightBlockState::Normal) {
                m_codeBlocksState.insert(blockNumber, state);
            }

            block = block.next();
        }
    }
}

static inline bool isDisplayFormulaRawEnd(const QString &p_text)
{
    QRegExp regex("\\\\end\\{[^{}\\s\\r\\n]+\\}$");
    if (p_text.indexOf(regex) > -1) {
        return true;
    }

    return false;
}

void PegHighlighterResult::parseMathjaxBlocks(const PegMarkdownHighlighter *p_peg,
                                              const QSharedPointer<PegParseResult> &p_result)
{
    const QTextDocument *doc = p_peg->getDocument();

    // Inline equations.
    const QVector<VElementRegion> &inlineRegs = p_result->m_inlineEquationRegions;

    for (auto it = inlineRegs.begin(); it != inlineRegs.end(); ++it) {
        const VElementRegion &r = *it;
        QTextBlock block = doc->findBlock(r.m_startPos);
        if (!block.isValid()) {
            continue;
        }

        // Inline equation MUST in one block.
        if (r.m_endPos - block.position() > block.length()) {
            continue;
        }

        VMathjaxBlock item;
        item.m_blockNumber = block.blockNumber();
        item.m_previewedAsBlock = false;
        item.m_index = r.m_startPos - block.position();
        item.m_length = r.m_endPos - r.m_startPos;
        item.m_text = block.text().mid(item.m_index, item.m_length);
        m_mathjaxBlocks.append(item);
    }

    // Display formulas.
    // One block may be split into several regions due to list indentation.
    const QVector<VElementRegion> &formulaRegs = p_result->m_displayFormulaRegions;
    VMathjaxBlock item;
    bool inBlock = false;
    QString marker("$$");
    QString rawMarkerStart("\\begin{");
    for (auto it = formulaRegs.begin(); it != formulaRegs.end(); ++it) {
        const VElementRegion &r = *it;
        QTextBlock block = doc->findBlock(r.m_startPos);
        int lastBlock = doc->findBlock(r.m_endPos - 1).blockNumber();
        if (lastBlock >= p_result->m_numOfBlocks) {
            lastBlock = p_result->m_numOfBlocks - 1;
        }

        while (block.isValid()) {
            int blockNum = block.blockNumber();
            if (blockNum > lastBlock) {
                break;
            }

            int pib = r.m_startPos - block.position();
            int length = r.m_endPos - r.m_startPos;
            QString text = block.text().mid(pib, length);
            if (inBlock) {
                item.m_text = item.m_text + "\n" + text;
                if (text.endsWith(marker)
                    || (blockNum == lastBlock && isDisplayFormulaRawEnd(text))) {
                    // End of block.
                    inBlock = false;
                    item.m_blockNumber = blockNum;
                    item.m_index = pib;
                    item.m_length = length;
                    m_mathjaxBlocks.append(item);
                }
            } else {
                if (!text.startsWith(marker)
                    && !text.startsWith(rawMarkerStart)) {
                    break;
                }

                if ((text.size() > 2 && text.endsWith(marker))
                    || (blockNum == lastBlock && isDisplayFormulaRawEnd(text))) {
                    // Within one block.
                    item.m_blockNumber = blockNum;
                    item.m_previewedAsBlock = true;
                    item.m_index = pib;
                    item.m_length = length;
                    item.m_text = text;
                    m_mathjaxBlocks.append(item);
                } else {
                    inBlock = true;
                    item.m_previewedAsBlock = true;
                    item.m_text = text;
                }
            }

            block = block.next();
        }
    }
}

void PegHighlighterResult::parseHRuleBlocks(const PegMarkdownHighlighter *p_peg,
                                            const QSharedPointer<PegParseResult> &p_result)
{
    const QTextDocument *doc = p_peg->getDocument();
    const QVector<VElementRegion> &regs = p_result->m_hruleRegions;

    for (auto it = regs.begin(); it != regs.end(); ++it) {
        QTextBlock block = doc->findBlock(it->m_startPos);
        int lastBlock = doc->findBlock(it->m_endPos - 1).blockNumber();
        if (lastBlock >= p_result->m_numOfBlocks) {
            lastBlock = p_result->m_numOfBlocks - 1;
        }

        while (block.isValid()) {
            int blockNumber = block.blockNumber();
            if (blockNumber > lastBlock) {
                break;
            }

            m_hruleBlocks.insert(blockNumber);

            block = block.next();
        }
    }
}
