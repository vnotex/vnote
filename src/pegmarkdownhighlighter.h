#ifndef PEGMARKDOWNHIGHLIGHTER_H
#define PEGMARKDOWNHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include "vtextblockdata.h"
#include "markdownhighlighterdata.h"
#include "peghighlighterresult.h"

class PegParser;
class QTimer;
class VMdEditor;

class PegMarkdownHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    PegMarkdownHighlighter(QTextDocument *p_doc, VMdEditor *p_editor);

    void init(const QVector<HighlightingStyle> &p_styles,
              const QHash<QString, QTextCharFormat> &p_codeBlockStyles,
              bool p_mathjaxEnabled,
              int p_timerInterval);

    // Set code block highlight result by VCodeBlockHighlightHelper.
    void setCodeBlockHighlights(TimeStamp p_timeStamp, const QVector<HLUnitPos> &p_units);

    const QVector<VElementRegion> &getHeaderRegions() const;

    const QSet<int> &getPossiblePreviewBlocks() const;

    void clearPossiblePreviewBlocks(const QVector<int> &p_blocksToClear);

    void addPossiblePreviewBlock(int p_blockNumber);

    QHash<QString, QTextCharFormat> &getCodeBlockStyles();

    QVector<HighlightingStyle> &getStyles();

    const QVector<HighlightingStyle> &getStyles() const;

    const QTextDocument *getDocument() const;

public slots:
    // Parse and rehighlight immediately.
    void updateHighlight();

    // Rehighlight sensitive blocks using current parse result, mainly
    // visible blocks.
    void rehighlightSensitiveBlocks();

signals:
    void highlightCompleted();

    // QVector is implicitly shared.
    void codeBlocksUpdated(TimeStamp p_timeStamp, const QVector<VCodeBlock> &p_codeBlocks);

    // Emitted when image regions have been fetched from a new parsing result.
    void imageLinksUpdated(const QVector<VElementRegion> &p_imageRegions);

    // Emitted when header regions have been fetched from a new parsing result.
    void headersUpdated(const QVector<VElementRegion> &p_headerRegions);

    // Emitted when Mathjax blocks updated.
    void mathjaxBlocksUpdated(const QVector<VMathjaxBlock> &p_mathjaxBlocks);

protected:
    void highlightBlock(const QString &p_text) Q_DECL_OVERRIDE;

private slots:
    void handleContentsChange(int p_position, int p_charsRemoved, int p_charsAdded);

    void handleParseResult(const QSharedPointer<PegParseResult> &p_result);

private:
    void startParse();

    void startFastParse(int p_position, int p_charsRemoved, int p_charsAdded);

    void clearAllBlocksUserDataAndState(const QSharedPointer<PegHighlighterResult> &p_result);

    void updateAllBlocksUserState(const QSharedPointer<PegHighlighterResult> &p_result);

    void updateCodeBlocks(const QSharedPointer<PegHighlighterResult> &p_result);

    void clearBlockUserData(const QSharedPointer<PegHighlighterResult> &p_result,
                            QTextBlock &p_block);

    // Highlight fenced code block according to VCodeBlockHighlightHelper result.
    void highlightCodeBlock(const QSharedPointer<PegHighlighterResult> &p_result,
                            int p_blockNum,
                            const QString &p_text);

    // Highlight color column in code block.
    void highlightCodeBlockColorColumn(const QString &p_text);

    VTextBlockData *currentBlockData() const;

    VTextBlockData *previousBlockData() const;

    VTextBlockData *previousBlockData(const QTextBlock &p_block) const;

    void completeHighlight(QSharedPointer<PegHighlighterResult> p_result);

    bool isMathJaxEnabled() const;

    void getFastParseBlockRange(int p_position,
                                int p_charsRemoved,
                                int p_charsAdded,
                                int &p_firstBlock,
                                int &p_lastBlock) const;

    void processFastParseResult(const QSharedPointer<PegParseResult> &p_result);

    bool highlightBlockOne(const QVector<QVector<HLUnit>> &p_highlights,
                           int p_blockNum);

    // To avoid line height jitter.
    void preHighlightSingleFormatBlock(const QVector<QVector<HLUnit>> &p_highlights,
                                       int p_blockNum,
                                       const QString &p_text);

    void updateSingleFormatBlocks(const QVector<QVector<HLUnit>> &p_highlights);

    void rehighlightBlocks();

    bool rehighlightBlockRange(int p_first, int p_last);

    static bool isEmptyCodeBlockHighlights(const QVector<QVector<HLUnitStyle>> &p_highlights);

    static TimeStamp blockTimeStamp(const QTextBlock &p_block);

    static void updateBlockTimeStamp(const QTextBlock &p_block, TimeStamp p_ts);

    static TimeStamp blockCodeBlockTimeStamp(const QTextBlock &p_block);

    static void updateBlockCodeBlockTimeStamp(const QTextBlock &p_block, TimeStamp p_ts);

    QTextDocument *m_doc;

    VMdEditor *m_editor;

    TimeStamp m_timeStamp;

    QVector<HighlightingStyle> m_styles;
    QHash<QString, QTextCharFormat> m_codeBlockStyles;

    QTextCharFormat m_codeBlockFormat;
    QTextCharFormat m_colorColumnFormat;

    PegParser *m_parser;

    QSharedPointer<PegHighlighterResult> m_result;

    QSharedPointer<PegHighlighterFastResult> m_fastResult;

    // Block number of those blocks which possible contains previewed image.
    QSet<int> m_possiblePreviewBlocks;

    // Extensions for parser.
    int m_parserExts;

    // Timer to trigger parse.
    QTimer *m_timer;

    QTimer *m_fastParseTimer;

    QTimer *m_rehighlightTimer;

    // Blocks have only one format set which occupies the whole block.
    QSet<int> m_singleFormatBlocks;
};

inline const QVector<VElementRegion> &PegMarkdownHighlighter::getHeaderRegions() const
{
    return m_result->m_headerRegions;
}

inline const QSet<int> &PegMarkdownHighlighter::getPossiblePreviewBlocks() const
{
    return m_possiblePreviewBlocks;
}

inline void PegMarkdownHighlighter::clearPossiblePreviewBlocks(const QVector<int> &p_blocksToClear)
{
    for (auto i : p_blocksToClear) {
        m_possiblePreviewBlocks.remove(i);
    }
}

inline void PegMarkdownHighlighter::addPossiblePreviewBlock(int p_blockNumber)
{
    m_possiblePreviewBlocks.insert(p_blockNumber);
}

inline QHash<QString, QTextCharFormat> &PegMarkdownHighlighter::getCodeBlockStyles()
{
    return m_codeBlockStyles;
}

inline QVector<HighlightingStyle> &PegMarkdownHighlighter::getStyles()
{
    return m_styles;
}

inline const QVector<HighlightingStyle> &PegMarkdownHighlighter::getStyles() const
{
    return m_styles;
}

inline const QTextDocument *PegMarkdownHighlighter::getDocument() const
{
    return m_doc;
}

inline VTextBlockData *PegMarkdownHighlighter::currentBlockData() const
{
    return static_cast<VTextBlockData *>(currentBlockUserData());
}

inline VTextBlockData *PegMarkdownHighlighter::previousBlockData() const
{
    QTextBlock block = currentBlock().previous();
    if (!block.isValid()) {
        return NULL;
    }

    return static_cast<VTextBlockData *>(block.userData());
}

inline VTextBlockData *PegMarkdownHighlighter::previousBlockData(const QTextBlock &p_block) const
{
    if (!p_block.isValid()) {
        return NULL;
    }

    QTextBlock block = p_block.previous();
    if (!block.isValid()) {
        return NULL;
    }

    return static_cast<VTextBlockData *>(block.userData());
}

inline bool PegMarkdownHighlighter::isMathJaxEnabled() const
{
    return m_parserExts & pmh_EXT_MATH;
}

inline TimeStamp PegMarkdownHighlighter::blockTimeStamp(const QTextBlock &p_block)
{
    VTextBlockData *data = static_cast<VTextBlockData *>(p_block.userData());
    if (data) {
        return data->getTimeStamp();
    } else {
        return 0;
    }
}

inline void PegMarkdownHighlighter::updateBlockTimeStamp(const QTextBlock &p_block, TimeStamp p_ts)
{
    VTextBlockData *data = static_cast<VTextBlockData *>(p_block.userData());
    if (data) {
        data->setTimeStamp(p_ts);
    }
}

inline TimeStamp PegMarkdownHighlighter::blockCodeBlockTimeStamp(const QTextBlock &p_block)
{
    VTextBlockData *data = static_cast<VTextBlockData *>(p_block.userData());
    if (data) {
        return data->getCodeBlockTimeStamp();
    } else {
        return 0;
    }
}

inline void PegMarkdownHighlighter::updateBlockCodeBlockTimeStamp(const QTextBlock &p_block, TimeStamp p_ts)
{
    VTextBlockData *data = static_cast<VTextBlockData *>(p_block.userData());
    if (data) {
        data->setCodeBlockTimeStamp(p_ts);
    }
}

inline bool PegMarkdownHighlighter::isEmptyCodeBlockHighlights(const QVector<QVector<HLUnitStyle>> &p_highlights)
{
    if (p_highlights.isEmpty()) {
        return true;
    }

    bool empty = true;
    for (int i = 0; i < p_highlights.size(); ++i) {
        if (!p_highlights[i].isEmpty()) {
            empty = false;
            break;
        }
    }

    return empty;
}
#endif // PEGMARKDOWNHIGHLIGHTER_H
