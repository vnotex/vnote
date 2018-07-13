#include "peghighlighterresult.h"

#include <QTextDocument>
#include <QTextBlock>
#include <QDebug>

#include "pegmarkdownhighlighter.h"
#include "utils/vutils.h"

PegHighlighterResult::PegHighlighterResult()
    : m_timeStamp(0),
      m_numOfBlocks(0),
      m_numOfCodeBlockHighlightsToRecv(0)
{
    m_codeBlockStartExp = QRegExp(VUtils::c_fencedCodeBlockStartRegExp);
    m_codeBlockEndExp = QRegExp(VUtils::c_fencedCodeBlockEndRegExp);
}

PegHighlighterResult::PegHighlighterResult(const PegMarkdownHighlighter *p_peg,
                                           const QSharedPointer<PegParseResult> &p_result)
    : m_timeStamp(p_result->m_timeStamp),
      m_numOfBlocks(p_result->m_numOfBlocks),
      m_numOfCodeBlockHighlightsToRecv(0)
{
    m_codeBlockStartExp = QRegExp(VUtils::c_fencedCodeBlockStartRegExp);
    m_codeBlockEndExp = QRegExp(VUtils::c_fencedCodeBlockEndRegExp);

    parseBlocksHighlights(p_peg, p_result);

    // Implicit sharing.
    m_imageRegions = p_result->m_imageRegions;
    m_headerRegions = p_result->m_headerRegions;

    parseFencedCodeBlocks(p_peg, p_result);

    parseMathjaxBlocks(p_peg, p_result);
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

void PegHighlighterResult::parseBlocksHighlights(const PegMarkdownHighlighter *p_peg,
                                                 const QSharedPointer<PegParseResult> &p_result)
{
    m_blocksHighlights.resize(m_numOfBlocks);
    if (p_result->isEmpty()) {
        return;
    }

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

            parseBlocksHighlightOne(doc, elem_cursor->pos, elem_cursor->end, i);
            elem_cursor = elem_cursor->next;
        }
    }

    // Sort m_blocksHighlights.
    for (int i = 0; i < m_blocksHighlights.size(); ++i) {
        if (m_blocksHighlights[i].size() > 1) {
            std::sort(m_blocksHighlights[i].begin(), m_blocksHighlights[i].end(), compHLUnit);
        }
    }
}

void PegHighlighterResult::parseBlocksHighlightOne(const QTextDocument *p_doc,
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
    int endBlockNum = p_doc->findBlock(p_end).blockNumber();
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

        m_blocksHighlights[blockNum].append(unit);

        block = block.next();
    }
}

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
    int endBlockNum = p_doc->findBlock(p_end).blockNumber();
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

void PegHighlighterResult::parseFencedCodeBlocks(const PegMarkdownHighlighter *p_peg,
                                                 const QSharedPointer<PegParseResult> &p_result)
{
    const QMap<int, VElementRegion> &regs = p_result->m_codeBlockRegions;

    const QTextDocument *doc = p_peg->getDocument();
    VCodeBlock item;
    bool inBlock = false;
    for (auto it = regs.begin(); it != regs.end(); ++it) {
        QTextBlock block = doc->findBlock(it.value().m_startPos);
        int lastBlock = doc->findBlock(it.value().m_endPos - 1).blockNumber();

        while (block.isValid()) {
            int blockNumber = block.blockNumber();
            if (blockNumber > lastBlock) {
                break;
            }

            HighlightBlockState state = HighlightBlockState::Normal;
            QString text = block.text();
            if (inBlock) {
                item.m_text = item.m_text + "\n" + text;
                int idx = m_codeBlockEndExp.indexIn(text);
                if (idx >= 0) {
                    // End block.
                    inBlock = false;
                    state = HighlightBlockState::CodeBlockEnd;
                    item.m_endBlock = blockNumber;
                    m_codeBlocks.append(item);
                } else {
                    // Within code block.
                    state = HighlightBlockState::CodeBlock;
                }
            } else {
                int idx = m_codeBlockStartExp.indexIn(text);
                if (idx >= 0) {
                    // Start block.
                    inBlock = true;
                    state = HighlightBlockState::CodeBlockStart;
                    item.m_startBlock = blockNumber;
                    item.m_startPos = block.position();
                    item.m_text = text;
                    if (m_codeBlockStartExp.captureCount() == 2) {
                        item.m_lang = m_codeBlockStartExp.capturedTexts()[2];
                    }
                }
            }

            if (state != HighlightBlockState::Normal) {
                m_codeBlocksState.insert(blockNumber, state);
            }

            block = block.next();
        }
    }
}

void PegHighlighterResult::parseMathjaxBlocks(const PegMarkdownHighlighter *p_peg,
                                              const QSharedPointer<PegParseResult> &p_result)
{
    const QTextDocument *doc = p_peg->getDocument();

    // Inline equations.
    const QVector<VElementRegion> inlineRegs = p_result->m_inlineEquationRegions;

    for (auto it = inlineRegs.begin(); it != inlineRegs.end(); ++it) {
        const VElementRegion &r = *it;
        QTextBlock block = doc->findBlock(r.m_startPos);
        // Inline equation MUST in one block.
        Q_ASSERT(block.blockNumber() == doc->findBlock(r.m_endPos - 1).blockNumber());

        if (!block.isValid()) {
            continue;
        }

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
    const QVector<VElementRegion> formulaRegs = p_result->m_displayFormulaRegions;
    VMathjaxBlock item;
    bool inBlock = false;
    QString marker("$$");
    for (auto it = formulaRegs.begin(); it != formulaRegs.end(); ++it) {
        const VElementRegion &r = *it;
        QTextBlock block = doc->findBlock(r.m_startPos);
        int lastBlock = doc->findBlock(r.m_endPos - 1).blockNumber();

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
                if (text.endsWith(marker)) {
                    // End of block.
                    inBlock = false;
                    item.m_blockNumber = blockNum;
                    item.m_index = pib;
                    item.m_length = length;
                    m_mathjaxBlocks.append(item);
                }
            } else {
                Q_ASSERT(text.startsWith(marker));
                if (text.size() > 2 && text.endsWith(marker)) {
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
