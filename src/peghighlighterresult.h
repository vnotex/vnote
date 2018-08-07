#ifndef PEGHIGHLIGHTERRESULT_H
#define PEGHIGHLIGHTERRESULT_H

#include <QSet>

#include "vconstants.h"
#include "pegparser.h"

class PegMarkdownHighlighter;
class QTextDocument;

class PegHighlighterFastResult
{
public:
    PegHighlighterFastResult();

    PegHighlighterFastResult(const PegMarkdownHighlighter *p_peg,
                             const QSharedPointer<PegParseResult> &p_result);

    bool matched(TimeStamp p_timeStamp) const
    {
        return m_timeStamp == p_timeStamp;
    }

    void clear()
    {
        m_blocksHighlights.clear();
    }

    TimeStamp m_timeStamp;

    QVector<QVector<HLUnit>> m_blocksHighlights;
};


class PegHighlighterResult
{
public:
    PegHighlighterResult();

    // TODO: handle p_result->m_offset.
    PegHighlighterResult(const PegMarkdownHighlighter *p_peg,
                         const QSharedPointer<PegParseResult> &p_result);

    bool matched(TimeStamp p_timeStamp) const;

    // Parse highlight elements for all the blocks from parse results.
    static void parseBlocksHighlights(QVector<QVector<HLUnit>> &p_blocksHighlights,
                                      const PegMarkdownHighlighter *p_peg,
                                      const QSharedPointer<PegParseResult> &p_result);

    TimeStamp m_timeStamp;

    int m_numOfBlocks;

    QVector<QVector<HLUnit>> m_blocksHighlights;

    // Use another member to store the codeblocks highlights, because the highlight
    // sequence is blockHighlights, regular-expression-based highlihgts, and then
    // codeBlockHighlights.
    // Support fenced code block only.
    QVector<QVector<HLUnitStyle> > m_codeBlocksHighlights;

    // Whether the code block highlight results of this result have been received.
    bool m_codeBlockHighlightReceived;

    // Timestamp for m_codeBlocksHighlights.
    TimeStamp m_codeBlockTimeStamp;

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

    // All MathJax blocks.
    QVector<VMathjaxBlock> m_mathjaxBlocks;

    QSet<int> m_hruleBlocks;

private:
    // Parse highlight elements for blocks from one parse result.
    static void parseBlocksHighlightOne(QVector<QVector<HLUnit>> &p_blocksHighlights,
                                        const QTextDocument *p_doc,
                                        unsigned long p_pos,
                                        unsigned long p_end,
                                        int p_styleIndex);

    // Parse fenced code blocks from parse results.
    void parseFencedCodeBlocks(const PegMarkdownHighlighter *p_peg,
                               const QSharedPointer<PegParseResult> &p_result);

    // Parse mathjax blocks from parse results.
    void parseMathjaxBlocks(const PegMarkdownHighlighter *p_peg,
                            const QSharedPointer<PegParseResult> &p_result);

    // Parse HRule blocks from parse results.
    void parseHRuleBlocks(const PegMarkdownHighlighter *p_peg,
                          const QSharedPointer<PegParseResult> &p_result);

#if 0
    void parseBlocksElementRegionOne(QHash<int, QVector<VElementRegion>> &p_regs,
                                     const QTextDocument *p_doc,
                                     unsigned long p_pos,
                                     unsigned long p_end);
#endif

    QRegExp m_codeBlockStartExp;
    QRegExp m_codeBlockEndExp;
};

inline bool PegHighlighterResult::matched(TimeStamp p_timeStamp) const
{
    return m_timeStamp == p_timeStamp;
}
#endif // PEGHIGHLIGHTERRESULT_H
