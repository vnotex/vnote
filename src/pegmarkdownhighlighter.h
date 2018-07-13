#ifndef PEGMARKDOWNHIGHLIGHTER_H
#define PEGMARKDOWNHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include "vtextblockdata.h"
#include "markdownhighlighterdata.h"
#include "peghighlighterresult.h"

class PegParser;
class QTimer;

class PegMarkdownHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit PegMarkdownHighlighter(QTextDocument *p_doc = nullptr);

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

    // Parse and only update the highlight results for rehighlight().
    void updateHighlightFast();

    QHash<QString, QTextCharFormat> &getCodeBlockStyles();

    QVector<HighlightingStyle> &getStyles();

    const QVector<HighlightingStyle> &getStyles() const;

    const QTextDocument *getDocument() const;

public slots:
    // Parse and rehighlight immediately.
    void updateHighlight();

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

    void updateCodeBlocks(QSharedPointer<PegHighlighterResult> p_result);

    // Set the user data of currentBlock().
    void updateBlockUserData(int p_blockNum, const QString &p_text);

    void updateCodeBlockState(const QSharedPointer<PegHighlighterResult> &p_result,
                              int p_blockNum,
                              const QString &p_text);

    // Highlight fenced code block according to VCodeBlockHighlightHelper result.
    void highlightCodeBlock(const QSharedPointer<PegHighlighterResult> &p_result,
                            int p_blockNum,
                            const QString &p_text);

    // Highlight color column in code block.
    void highlightCodeBlockColorColumn(const QString &p_text);

    VTextBlockData *currentBlockData() const;

    VTextBlockData *previousBlockData() const;

    void completeHighlight(QSharedPointer<PegHighlighterResult> p_result);

    bool isMathJaxEnabled() const;

    QTextDocument *m_doc;

    TimeStamp m_timeStamp;

    QVector<HighlightingStyle> m_styles;
    QHash<QString, QTextCharFormat> m_codeBlockStyles;

    QTextCharFormat m_codeBlockFormat;
    QTextCharFormat m_colorColumnFormat;

    PegParser *m_parser;

    QSharedPointer<PegHighlighterResult> m_result;

    // Block number of those blocks which possible contains previewed image.
    QSet<int> m_possiblePreviewBlocks;

    // Extensions for parser.
    int m_parserExts;

    // Timer to trigger parse.
    QTimer *m_timer;
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

inline bool PegMarkdownHighlighter::isMathJaxEnabled() const
{
    return m_parserExts & pmh_EXT_MATH;
}
#endif // PEGMARKDOWNHIGHLIGHTER_H
