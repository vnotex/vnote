#ifndef PEGHIGHLIGHTERRESULT_H
#define PEGHIGHLIGHTERRESULT_H

#include "vconstants.h"
#include "pegparser.h"

class PegMarkdownHighlighter;
class QTextDocument;

class PegHighlighterResult
{
public:
    PegHighlighterResult();

    PegHighlighterResult(const PegMarkdownHighlighter *p_peg,
                         const QSharedPointer<PegParseResult> &p_result);

    bool matched(TimeStamp p_timeStamp) const;

    TimeStamp m_timeStamp;

    int m_numOfBlocks;

    QVector<QVector<HLUnit>> m_blocksHighlights;

    // Use another member to store the codeblocks highlights, because the highlight
    // sequence is blockHighlights, regular-expression-based highlihgts, and then
    // codeBlockHighlights.
    // Support fenced code block only.
    QVector<QVector<HLUnitStyle> > m_codeBlocksHighlights;

    // All image link regions.
    QVector<VElementRegion> m_imageRegions;

    // All header regions.
    // Sorted by start position.
    QVector<VElementRegion> m_headerRegions;

    // All fenced code blocks.
    QVector<VCodeBlock> m_codeBlocks;

    // Indexed by block number.
    QHash<int, HighlightBlockState> m_codeBlocksState;

    int m_numOfCodeBlockHighlightsToRecv;

private:
    // Parse highlight elements for all the blocks from parse results.
    void parseBlocksHighlights(const PegMarkdownHighlighter *p_peg,
                               const QSharedPointer<PegParseResult> &p_result);

    // Parse highlight elements for blocks from one parse result.
    void parseBlocksHighlightOne(const QTextDocument *p_doc,
                                 unsigned long p_pos,
                                 unsigned long p_end,
                                 int p_styleIndex);

    // Parse fenced code blocks from parse results.
    void parseFencedCodeBlocks(const PegMarkdownHighlighter *p_peg,
                               const QSharedPointer<PegParseResult> &p_result);

    void parseBlocksElementRegionOne(QHash<int, QVector<VElementRegion>> &p_regs,
                                     const QTextDocument *p_doc,
                                     unsigned long p_pos,
                                     unsigned long p_end);

    QRegExp m_codeBlockStartExp;
    QRegExp m_codeBlockEndExp;
};

inline bool PegHighlighterResult::matched(TimeStamp p_timeStamp) const
{
    return m_timeStamp == p_timeStamp;
}
#endif // PEGHIGHLIGHTERRESULT_H
