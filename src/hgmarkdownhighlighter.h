#ifndef HGMARKDOWNHIGHLIGHTER_H
#define HGMARKDOWNHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QAtomicInt>
#include <QMap>
#include <QSet>
#include <QString>

#include "vtextblockdata.h"
#include "markdownhighlighterdata.h"

class QTextDocument;


class HGMarkdownHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    HGMarkdownHighlighter(const QVector<HighlightingStyle> &styles,
                          const QHash<QString, QTextCharFormat> &codeBlockStyles,
                          int waitInterval,
                          QTextDocument *parent = 0);
    ~HGMarkdownHighlighter();

    // Request to update highlihgt (re-parse and re-highlight)
    void setCodeBlockHighlights(const QVector<HLUnitPos> &p_units);

    const QVector<VElementRegion> &getHeaderRegions() const;

    const QSet<int> &getPossiblePreviewBlocks() const;

    void clearPossiblePreviewBlocks(const QVector<int> &p_blocksToClear);

    void addPossiblePreviewBlock(int p_blockNumber);

    // Parse and only update the highlight results for rehighlight().
    void updateHighlightFast();

    QHash<QString, QTextCharFormat> &getCodeBlockStyles();

    QVector<HighlightingStyle> &getHighlightingStyles();

    void setMathjaxEnabled(bool p_enabled);

signals:
    void highlightCompleted();

    // QVector is implicitly shared.
    void codeBlocksUpdated(const QVector<VCodeBlock> &p_codeBlocks);

    // Emitted when image regions have been fetched from a new parsing result.
    void imageLinksUpdated(const QVector<VElementRegion> &p_imageRegions);

    // Emitted when header regions have been fetched from a new parsing result.
    void headersUpdated(const QVector<VElementRegion> &p_headerRegions);

    // Emitted when Mathjax blocks updated.
    void mathjaxBlocksUpdated(const QVector<VMathjaxBlock> &p_mathjaxBlocks);

protected:
    void highlightBlock(const QString &text) Q_DECL_OVERRIDE;

public slots:
    // Parse and rehighlight immediately.
    void updateHighlight();

private slots:
    void handleContentChange(int position, int charsRemoved, int charsAdded);

    // @p_fast: if true, just parse and update styles.
    void startParseAndHighlight(bool p_fast = false);

    void completeHighlight();

private:
    struct HeaderBlockInfo
    {
        HeaderBlockInfo(int p_level = -1, int p_length = 0)
            : m_level(p_level), m_length(p_length)
        {
        }

        // Header level based on 0.
        int m_level;

        // Block length;
        int m_length;
    };

    TimeStamp m_timeStamp;

    QRegExp codeBlockStartExp;
    QRegExp codeBlockEndExp;

    QRegExp m_mathjaxInlineExp;
    QRegExp m_mathjaxBlockExp;

    QTextCharFormat m_codeBlockFormat;
    QTextCharFormat m_linkFormat;
    QTextCharFormat m_imageFormat;
    QTextCharFormat m_colorColumnFormat;
    QTextCharFormat m_mathjaxFormat;

    QTextDocument *document;

    QVector<HighlightingStyle> highlightingStyles;

    QHash<QString, QTextCharFormat> m_codeBlockStyles;

    QVector<QVector<HLUnit> > m_blockHighlights;

    // Used for cache, [0, 6].
    QVector<QTextCharFormat> m_headerStyles;

    // Use another member to store the codeblocks highlights, because the highlight
    // sequence is blockHighlights, regular-expression-based highlihgts, and then
    // codeBlockHighlights.
    // Support fenced code block only.
    QVector<QVector<HLUnitStyle> > m_codeBlockHighlights;

    int m_numOfCodeBlockHighlightsToRecv;

    // All image link regions.
    QVector<VElementRegion> m_imageRegions;

    // All header regions.
    // May contains illegal elements.
    // Sorted by start position.
    QVector<VElementRegion> m_headerRegions;

    // All verbatim blocks (by parser) number.
    QSet<int> m_verbatimBlocks;

    // All fenced code blocks.
    QVector<VCodeBlock> m_codeBlocks;

    // Indexed by block number.
    QHash<int, HighlightBlockState> m_codeBlocksState;

    // Indexed by block number.
    QHash<int, HeaderBlockInfo> m_headerBlocks;

    // Timer to signal highlightCompleted().
    QTimer *m_completeTimer;

    QAtomicInt parsing;

    // Whether highlight results for blocks are ready.
    bool m_blockHLResultReady;

    QTimer *timer;
    int waitInterval;

    // Block number of those blocks which possible contains previewed image.
    QSet<int> m_possiblePreviewBlocks;

    bool m_enableMathjax;

    // Inline code regions for each block.
    // VElementRegion's position is relative position within a block.
    QHash<int, QVector<VElementRegion>> m_inlineCodeRegions;

    QHash<int, QVector<VElementRegion>> m_boldItalicRegions;

    // Including links and images.
    QHash<int, QVector<VElementRegion>> m_linkRegions;

    // Comment regions for each block.
    QHash<int, QVector<VElementRegion>> m_commentRegions;

    // Whether need to signal out changes when highlight completes.
    bool m_signalOut;

    char *content;
    int capacity;
    pmh_element **result;

    static const int initCapacity;

    void resizeBuffer(int newCap);

    void highlightCodeBlock(int p_blockNumber, const QString &p_text);

    void highlightMathJax(const QTextBlock &p_block, const QString &p_text);

    void parse();

    void parseInternal();

    // Init highlight elements for all the blocks from parse results.
    void initBlockHighlightFromResult(int nrBlocks);

    // Init highlight elements for blocks from one parse result.
    void initBlockHighlihgtOne(unsigned long pos,
                               unsigned long end,
                               int styleIndex);

    // Return true if there are fenced code blocks and it will call rehighlight() later.
    // Return false if there is none.
    bool updateCodeBlocks();

    void updateMathjaxBlocks();

    // Fetch all the HTML comment regions from parsing result.
    void initHtmlCommentRegionsFromResult();

    // Fetch all the image link regions from parsing result.
    void initImageRegionsFromResult();

    // Fetch all the header regions from parsing result.
    void initHeaderRegionsFromResult();

    // Fetch all the verbatim blocks from parsing result.
    void initVerbatimBlocksFromResult();

    // Fetch all the fenced code blocks from parsing result.
    void initFencedCodeBlocksFromResult();

    // Fetch all the inlnie code regions from parsing result.
    void initInlineCodeRegionsFromResult();

    // Fetch all bold/italic regions from parsing result.
    void initBoldItalicRegionsFromResult();

    // Fetch all bold/italic regions from parsing result.
    void initLinkRegionsFromResult();

    void initBlockElementRegionOne(QHash<int, QVector<VElementRegion>> &p_regs,
                                   unsigned long p_pos,
                                   unsigned long p_end);

    // Whether @p_block is totally inside a HTML comment.
    bool isBlockInsideCommentRegion(const QTextBlock &p_block) const;

    // Whether @p_block is a Verbatim block.
    bool isVerbatimBlock(const QTextBlock &p_block) const;

    // Highlights have been changed. Try to signal highlightCompleted().
    void highlightChanged();

    // Set the user data of currentBlock().
    void updateBlockUserData(int p_blockNum, const QString &p_text);

    // Highlight color column in code block.
    void highlightCodeBlockColorColumn(const QString &p_text);

    bool isValidHeader(const QString &p_text);

    VTextBlockData *currentBlockData() const;

    VTextBlockData *previousBlockData() const;

    // Highlight headers using regular expression first instead of waiting for
    // another parse.
    void highlightHeaderFast(int p_blockNumber, const QString &p_text);

    int findMathjaxMarker(int p_blockNumber,
                          const QString &p_text,
                          int p_pos,
                          QRegExp &p_reg,
                          int p_markerLength);

    bool isValidMathjaxRegion(int p_blockNumber, int p_start, int p_end);
};

inline const QVector<VElementRegion> &HGMarkdownHighlighter::getHeaderRegions() const
{
    return m_headerRegions;
}

inline const QSet<int> &HGMarkdownHighlighter::getPossiblePreviewBlocks() const
{
    return m_possiblePreviewBlocks;
}

inline void HGMarkdownHighlighter::clearPossiblePreviewBlocks(const QVector<int> &p_blocksToClear)
{
    for (auto i : p_blocksToClear) {
        m_possiblePreviewBlocks.remove(i);
    }
}

inline void HGMarkdownHighlighter::addPossiblePreviewBlock(int p_blockNumber)
{
    m_possiblePreviewBlocks.insert(p_blockNumber);
}

inline VTextBlockData *HGMarkdownHighlighter::currentBlockData() const
{
    return static_cast<VTextBlockData *>(currentBlockUserData());
}

inline VTextBlockData *HGMarkdownHighlighter::previousBlockData() const
{
    QTextBlock block = currentBlock().previous();
    if (!block.isValid()) {
        return NULL;
    }

    return static_cast<VTextBlockData *>(block.userData());
}

inline QHash<QString, QTextCharFormat> &HGMarkdownHighlighter::getCodeBlockStyles()
{
    return m_codeBlockStyles;
}

inline QVector<HighlightingStyle> &HGMarkdownHighlighter::getHighlightingStyles()
{
    return highlightingStyles;
}

inline bool HGMarkdownHighlighter::isVerbatimBlock(const QTextBlock &p_block) const
{
    return m_verbatimBlocks.contains(p_block.blockNumber());
}

inline void HGMarkdownHighlighter::setMathjaxEnabled(bool p_enabled)
{
    m_enableMathjax = p_enabled;
}
#endif
