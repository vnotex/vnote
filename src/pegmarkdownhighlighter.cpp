#include "pegmarkdownhighlighter.h"

#include <QTextDocument>
#include <QTimer>
#include <QScrollBar>

#include "pegparser.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "utils/veditutils.h"
#include "vmdeditor.h"

extern VConfigManager *g_config;

#define LARGE_BLOCK_NUMBER 2000

PegMarkdownHighlighter::PegMarkdownHighlighter(QTextDocument *p_doc, VMdEditor *p_editor)
    : QSyntaxHighlighter(p_doc),
      m_doc(p_doc),
      m_editor(p_editor),
      m_timeStamp(0),
      m_parser(NULL),
      m_parserExts(pmh_EXT_NOTES | pmh_EXT_STRIKE | pmh_EXT_FRONTMATTER | pmh_EXT_MARK)
{
}

void PegMarkdownHighlighter::init(const QVector<HighlightingStyle> &p_styles,
                                  const QHash<QString, QTextCharFormat> &p_codeBlockStyles,
                                  bool p_mathjaxEnabled,
                                  int p_timerInterval)
{
    m_styles = p_styles;
    m_codeBlockStyles = p_codeBlockStyles;

    if (p_mathjaxEnabled) {
        m_parserExts |= pmh_EXT_MATH;
    }

    m_codeBlockFormat.setForeground(QBrush(Qt::darkYellow));
    for (int index = 0; index < m_styles.size(); ++index) {
        switch (m_styles[index].type) {
        case pmh_FENCEDCODEBLOCK:
            m_codeBlockFormat = m_styles[index].format;
            break;

        default:
            break;
        }
    }

    m_colorColumnFormat = m_codeBlockFormat;
    m_colorColumnFormat.setForeground(QColor(g_config->getEditorColorColumnFg()));
    m_colorColumnFormat.setBackground(QColor(g_config->getEditorColorColumnBg()));

    m_result.reset(new PegHighlighterResult());
    m_fastResult.reset(new PegHighlighterFastResult());

    m_parser = new PegParser(this);
    connect(m_parser, &PegParser::parseResultReady,
            this, &PegMarkdownHighlighter::handleParseResult);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(p_timerInterval);
    connect(m_timer, &QTimer::timeout,
            this, [this]() {
                startParse();
            });

    m_fastParseTimer = new QTimer(this);
    m_fastParseTimer->setSingleShot(true);
    m_fastParseTimer->setInterval(50);
    connect(m_fastParseTimer, &QTimer::timeout,
            this, [this]() {
                QSharedPointer<PegHighlighterFastResult> result(m_fastResult);
                if (!result->matched(m_timeStamp) || m_result->matched(m_timeStamp)) {
                    return;
                }

                const QVector<QVector<HLUnit>> &hls = result->m_blocksHighlights;
                for (int i = 0; i < hls.size(); ++i) {
                    if (!hls[i].isEmpty()) {
                        QTextBlock block = m_doc->findBlockByNumber(i);
                        if (PegMarkdownHighlighter::blockTimeStamp(block) != m_timeStamp) {
                            rehighlightBlock(block);
                        }
                    }
                }
            });

    m_rehighlightTimer = new QTimer(this);
    m_rehighlightTimer->setSingleShot(true);
    m_rehighlightTimer->setInterval(5);
    connect(m_rehighlightTimer, &QTimer::timeout,
            this, [this]() {
                if (m_result->m_numOfBlocks > LARGE_BLOCK_NUMBER) {
                    rehighlightSensitiveBlocks();
                }
            });

    connect(m_doc, &QTextDocument::contentsChange,
            this, &PegMarkdownHighlighter::handleContentsChange);

    connect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged,
            m_rehighlightTimer, static_cast<void(QTimer::*)()>(&QTimer::start));
}

// Just use parse results to highlight block.
// Do not maintain block data and state here.
void PegMarkdownHighlighter::highlightBlock(const QString &p_text)
{
    QSharedPointer<PegHighlighterResult> result(m_result);

    QTextBlock block = currentBlock();
    int blockNum = block.blockNumber();

    if (result->matched(m_timeStamp)) {
        preHighlightSingleFormatBlock(result->m_blocksHighlights, blockNum, p_text);

        highlightBlockOne(result->m_blocksHighlights, blockNum);
    } else {
        preHighlightSingleFormatBlock(m_fastResult->m_blocksHighlights, blockNum, p_text);

        // If fast result cover this block, we do not need to use the outdated one.
        if (!highlightBlockOne(m_fastResult->m_blocksHighlights, blockNum)) {
            highlightBlockOne(result->m_blocksHighlights, blockNum);
        }
    }

    if (currentBlockState() == HighlightBlockState::CodeBlock) {
        highlightCodeBlock(result, blockNum, p_text);
        highlightCodeBlockColorColumn(p_text);
        PegMarkdownHighlighter::updateBlockCodeBlockTimeStamp(block, result->m_codeBlockTimeStamp);
    }

    PegMarkdownHighlighter::updateBlockTimeStamp(block, result->m_timeStamp);
}

void PegMarkdownHighlighter::preHighlightSingleFormatBlock(const QVector<QVector<HLUnit>> &p_highlights,
                                                           int p_blockNum,
                                                           const QString &p_text)
{
    int sz = p_text.size();
    if (sz == 0) {
        return;
    }

    if (!m_singleFormatBlocks.contains(p_blockNum)) {
        return;
    }

    if (p_highlights.size() > p_blockNum) {
        const QVector<HLUnit> &units = p_highlights[p_blockNum];
        if (units.size() == 1) {
            const HLUnit &unit = units[0];
            if (unit.start == 0 && (int)unit.length < sz) {
                setFormat(unit.length, sz - unit.length, m_styles[unit.styleIndex].format);
            }
        }
    }
}

bool PegMarkdownHighlighter::highlightBlockOne(const QVector<QVector<HLUnit>> &p_highlights,
                                               int p_blockNum)
{
    bool highlighted = false;
    if (p_highlights.size() > p_blockNum) {
        // units are sorted by start position and length.
        const QVector<HLUnit> &units = p_highlights[p_blockNum];
        if (!units.isEmpty()) {
            highlighted = true;
            for (int i = 0; i < units.size(); ++i) {
                const HLUnit &unit = units[i];
                if (i == 0) {
                    // No need to merge format.
                    setFormat(unit.start,
                              unit.length,
                              m_styles[unit.styleIndex].format);
                } else {
                    QTextCharFormat newFormat = m_styles[unit.styleIndex].format;
                    for (int j = i - 1; j >= 0; --j) {
                        if (units[j].start + units[j].length <= unit.start) {
                            // It won't affect current unit.
                            continue;
                        } else {
                            // Merge the format.
                            QTextCharFormat tmpFormat(newFormat);
                            newFormat = m_styles[units[j].styleIndex].format;
                            // tmpFormat takes precedence.
                            newFormat.merge(tmpFormat);
                        }
                    }

                    setFormat(unit.start, unit.length, newFormat);
                }
            }
        }
    }

    return highlighted;
}

// highlightBlock() will be called before this function.
void PegMarkdownHighlighter::handleContentsChange(int p_position, int p_charsRemoved, int p_charsAdded)
{
    Q_UNUSED(p_position);

    if (p_charsRemoved == 0 && p_charsAdded == 0) {
        return;
    }

    ++m_timeStamp;

    if (m_timeStamp > 2) {
        startFastParse(p_position, p_charsRemoved, p_charsAdded);
    }

    // We still need a timer to start a complete parse.
    m_timer->start();
}

void PegMarkdownHighlighter::startParse()
{
    QSharedPointer<PegParseConfig> config(new PegParseConfig());
    config->m_timeStamp = m_timeStamp;
    config->m_data = m_doc->toPlainText().toUtf8();
    config->m_numOfBlocks = m_doc->blockCount();
    config->m_extensions = m_parserExts;

    m_parser->parseAsync(config);
}

void PegMarkdownHighlighter::startFastParse(int p_position, int p_charsRemoved, int p_charsAdded)
{
    // Get affected block range.
    int firstBlockNum, lastBlockNum;
    getFastParseBlockRange(p_position, p_charsRemoved, p_charsAdded, firstBlockNum, lastBlockNum);
    if (firstBlockNum == -1) {
        // We could not let m_fastResult NULL here.
        return;
    }

    QString text;
    QTextBlock block = m_doc->findBlockByNumber(firstBlockNum);
    int offset = block.position();
    while (block.isValid()) {
        int blockNum = block.blockNumber();
        if (blockNum > lastBlockNum) {
            break;
        } else if (blockNum == firstBlockNum) {
            text = block.text();
        } else {
            text = text + "\n" + block.text();
        }

        block = block.next();
    }

    QSharedPointer<PegParseConfig> config(new PegParseConfig());
    config->m_timeStamp = m_timeStamp;
    config->m_data = text.toUtf8();
    config->m_numOfBlocks = m_doc->blockCount();
    config->m_offset = offset;
    config->m_extensions = m_parserExts;
    config->m_fast = true;

    QSharedPointer<PegParseResult> parseRes = m_parser->parse(config);
    processFastParseResult(parseRes);
}

void PegMarkdownHighlighter::processFastParseResult(const QSharedPointer<PegParseResult> &p_result)
{
    m_fastParseTimer->stop();
    m_fastResult.reset(new PegHighlighterFastResult(this, p_result));
    // Add additional single format blocks.
    updateSingleFormatBlocks(m_fastResult->m_blocksHighlights);
    m_fastParseTimer->start();
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

void PegMarkdownHighlighter::setCodeBlockHighlights(TimeStamp p_timeStamp,
                                                    const QVector<HLUnitPos> &p_units)
{
    QSharedPointer<PegHighlighterResult> result(m_result);
    if (!result->matched(p_timeStamp)
        || result->m_numOfCodeBlockHighlightsToRecv <= 0) {
        return;
    }

    if (p_units.isEmpty()) {
        goto exit;
    }

    {
    QVector<QVector<HLUnitStyle>> highlights(result->m_codeBlocksHighlights.size());
    for (auto const &unit : p_units) {
        int pos = unit.m_position;
        int end = unit.m_position + unit.m_length;
        QTextBlock block = m_doc->findBlock(pos);
        int startBlockNum = block.blockNumber();
        int endBlockNum = m_doc->findBlock(end).blockNumber();

        // Text has been changed. Abandon the obsolete parsed result.
        if (startBlockNum == -1 || endBlockNum >= highlights.size()) {
            goto exit;
        }

        while (block.isValid()) {
            int blockNumber = block.blockNumber();
            if (blockNumber > endBlockNum) {
                break;
            }

            int blockStartPos = block.position();
            HLUnitStyle hl;
            hl.style = unit.m_style;
            if (blockNumber == startBlockNum) {
                hl.start = pos - blockStartPos;
                hl.length = (startBlockNum == endBlockNum) ?
                                (end - pos) : (block.length() - hl.start);
            } else if (blockNumber == endBlockNum) {
                hl.start = 0;
                hl.length = end - blockStartPos;
            } else {
                hl.start = 0;
                hl.length = block.length();
            }

            highlights[blockNumber].append(hl);

            block = block.next();
        }
    }

    // Need to highlight in order.
    for (int i = 0; i < highlights.size(); ++i) {
        QVector<HLUnitStyle> &units = highlights[i];
        if (!units.isEmpty()) {
            if (units.size() > 1) {
                std::sort(units.begin(), units.end(), compHLUnitStyle);
            }

            result->m_codeBlocksHighlights[i].append(units);
        }
    }
    }

exit:
    if (--result->m_numOfCodeBlockHighlightsToRecv <= 0) {
        ++result->m_codeBlockTimeStamp;
        rehighlightBlocks();
    }
}

void PegMarkdownHighlighter::updateHighlight()
{
    m_timer->stop();
    if (m_result->matched(m_timeStamp)) {
        // No need to parse again. Already the latest.
        updateCodeBlocks(m_result);
        rehighlightBlocks();
        completeHighlight(m_result);
    } else {
        startParse();
    }
}

void PegMarkdownHighlighter::handleParseResult(const QSharedPointer<PegParseResult> &p_result)
{
    if (!m_result.isNull() && m_result->m_timeStamp > p_result->m_timeStamp) {
        return;
    }

    m_result.reset(new PegHighlighterResult(this, p_result));

    m_singleFormatBlocks.clear();
    updateSingleFormatBlocks(m_result->m_blocksHighlights);

    bool matched = m_result->matched(m_timeStamp);
    if (matched) {
        clearAllBlocksUserDataAndState(m_result);

        updateAllBlocksUserState(m_result);

        updateCodeBlocks(m_result);
    }

    rehighlightBlocks();

    if (matched) {
        completeHighlight(m_result);
    }
}

void PegMarkdownHighlighter::updateSingleFormatBlocks(const QVector<QVector<HLUnit>> &p_highlights)
{
    for (int i = 0; i < p_highlights.size(); ++i) {
        const QVector<HLUnit> &units = p_highlights[i];
        if (units.size() == 1) {
            const HLUnit &unit = units[0];
            if (unit.start == 0 && unit.length > 0) {
                QTextBlock block = m_doc->findBlockByNumber(i);
                if (block.length() - 1 <= (int)unit.length) {
                    m_singleFormatBlocks.insert(i);
                }
            }
        }
    }
}

void PegMarkdownHighlighter::updateCodeBlocks(const QSharedPointer<PegHighlighterResult> &p_result)
{
    // Only need to receive code block highlights when it is empty.
    if (g_config->getEnableCodeBlockHighlight()
        && PegMarkdownHighlighter::isEmptyCodeBlockHighlights(p_result->m_codeBlocksHighlights)) {
        p_result->m_codeBlocksHighlights.resize(p_result->m_numOfBlocks);
        p_result->m_numOfCodeBlockHighlightsToRecv = p_result->m_codeBlocks.size();
    }

    emit codeBlocksUpdated(p_result->m_timeStamp, p_result->m_codeBlocks);
}

void PegMarkdownHighlighter::clearAllBlocksUserDataAndState(const QSharedPointer<PegHighlighterResult> &p_result)
{
    QTextBlock block = m_doc->firstBlock();
    while (block.isValid()) {
        clearBlockUserData(p_result, block);

        block.setUserState(HighlightBlockState::Normal);

        block = block.next();
    }
}

void PegMarkdownHighlighter::clearBlockUserData(const QSharedPointer<PegHighlighterResult> &p_result,
                                                QTextBlock &p_block)
{
    Q_UNUSED(p_result);

    int blockNum = p_block.blockNumber();

    VTextBlockData *blockData = static_cast<VTextBlockData *>(p_block.userData());
    if (!blockData) {
        blockData = new VTextBlockData();
        p_block.setUserData(blockData);

        m_possiblePreviewBlocks.remove(blockNum);
    } else {
        blockData->setCodeBlockIndentation(-1);

        if (blockData->getPreviews().isEmpty()) {
            m_possiblePreviewBlocks.remove(blockNum);
        } else {
            m_possiblePreviewBlocks.insert(blockNum);
        }
    }
}

void PegMarkdownHighlighter::updateAllBlocksUserState(const QSharedPointer<PegHighlighterResult> &p_result)
{
    // Code blocks.
    bool hlColumn = g_config->getColorColumn() > 0;
    const QHash<int, HighlightBlockState> &cbStates = p_result->m_codeBlocksState;
    for (auto it = cbStates.begin(); it != cbStates.end(); ++it) {
        QTextBlock block = m_doc->findBlockByNumber(it.key());
        if (!block.isValid()) {
            continue;
        }

        // Set code block indentation.
        if (hlColumn) {
            VTextBlockData *blockData = static_cast<VTextBlockData *>(block.userData());
            Q_ASSERT(blockData);

            switch (it.value()) {
            case HighlightBlockState::CodeBlockStart:
            {
                int startLeadingSpaces = 0;
                QRegExp reg(VUtils::c_fencedCodeBlockStartRegExp);
                int idx = reg.indexIn(block.text());
                if (idx >= 0) {
                    startLeadingSpaces = reg.capturedTexts()[1].size();
                }

                blockData->setCodeBlockIndentation(startLeadingSpaces);
                break;
            }

            case HighlightBlockState::CodeBlock:
                V_FALLTHROUGH;
            case HighlightBlockState::CodeBlockEnd:
            {
                int startLeadingSpaces = 0;
                VTextBlockData *preBlockData = previousBlockData(block);
                if (preBlockData) {
                    startLeadingSpaces = preBlockData->getCodeBlockIndentation();
                }

                blockData->setCodeBlockIndentation(startLeadingSpaces);
                break;
            }

            default:
                Q_ASSERT(false);
                break;
            }
        }

        block.setUserState(it.value());
    }

    // HRule blocks.
    foreach (int blk, p_result->m_hruleBlocks) {
        QTextBlock block = m_doc->findBlockByNumber(blk);
        if (block.isValid()) {
            block.setUserState(HighlightBlockState::HRule);
        }
    }
}

void PegMarkdownHighlighter::highlightCodeBlock(const QSharedPointer<PegHighlighterResult> &p_result,
                                                int p_blockNum,
                                                const QString &p_text)
{
    // Brush the indentation spaces.
    if (currentBlockState() == HighlightBlockState::CodeBlock) {
        int spaces = VEditUtils::fetchIndentation(p_text);
        if (spaces > 0) {
            setFormat(0, spaces, m_codeBlockFormat);
        }
    }

    if (p_result->m_codeBlocksHighlights.size() > p_blockNum) {
        const QVector<HLUnitStyle> &units = p_result->m_codeBlocksHighlights[p_blockNum];
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
}

void PegMarkdownHighlighter::highlightCodeBlockColorColumn(const QString &p_text)
{
    int cc = g_config->getColorColumn();
    if (cc <= 0) {
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

void PegMarkdownHighlighter::completeHighlight(QSharedPointer<PegHighlighterResult> p_result)
{
    if (isMathJaxEnabled()) {
        emit mathjaxBlocksUpdated(p_result->m_mathjaxBlocks);
    }

    emit imageLinksUpdated(p_result->m_imageRegions);
    emit headersUpdated(p_result->m_headerRegions);

    emit highlightCompleted();
}

void PegMarkdownHighlighter::getFastParseBlockRange(int p_position,
                                                    int p_charsRemoved,
                                                    int p_charsAdded,
                                                    int &p_firstBlock,
                                                    int &p_lastBlock) const
{
    const int maxNumOfBlocks = 100;

    int charsChanged = p_charsRemoved + p_charsAdded;
    QTextBlock firstBlock = m_doc->findBlock(p_position);

    // May be an invalid block.
    QTextBlock lastBlock = m_doc->findBlock(qMax(0, p_position + charsChanged));
    if (!lastBlock.isValid()) {
        lastBlock = m_doc->lastBlock();
    }

    int num = lastBlock.blockNumber() - firstBlock.blockNumber() + 1;
    if (num >= maxNumOfBlocks) {
        p_firstBlock = p_lastBlock = -1;
        return;
    }

    // Look up.
    // Find empty block.
    // When firstBlock is an empty block at first, we should always skip it.
    while (firstBlock.isValid() && num < maxNumOfBlocks) {
        QTextBlock block = firstBlock.previous();
        if (block.isValid() && !VEditUtils::isEmptyBlock(block)) {
            firstBlock = block;
            ++num;
        } else {
            break;
        }
    }

    // Cross code block.
    while (firstBlock.isValid() && num < maxNumOfBlocks) {
        int state = firstBlock.userState();
        if (state == HighlightBlockState::CodeBlock
            || state == HighlightBlockState::CodeBlockEnd) {
            QTextBlock block = firstBlock.previous();
            if (block.isValid()) {
                firstBlock = block;
                ++num;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    // Till the block with 0 indentation to handle contents in list.
    while (firstBlock.isValid() && num < maxNumOfBlocks) {
        if (VEditUtils::fetchIndentation(firstBlock) == 0
            && !VEditUtils::isEmptyBlock(firstBlock)) {
            break;
        } else {
            QTextBlock block = firstBlock.previous();
            if (block.isValid()) {
                firstBlock = block;
                ++num;
            } else {
                break;
            }
        }
    }

    // Look down.
    // Find empty block.
    // If lastBlock is an empty block at first, we should always skip it.
    while (lastBlock.isValid() && num < maxNumOfBlocks) {
        QTextBlock block = lastBlock.next();
        if (block.isValid() && !VEditUtils::isEmptyBlock(block)) {
            lastBlock = block;
            ++num;
        } else {
            break;
        }
    }

    // Cross code block.
    while (lastBlock.isValid() && num < maxNumOfBlocks) {
        int state = lastBlock.userState();
        if (state == HighlightBlockState::CodeBlock
            || state == HighlightBlockState::CodeBlockStart) {
            QTextBlock block = lastBlock.next();
            if (block.isValid()) {
                lastBlock = block;
                ++num;
            } else {
                break;
            }
        } else {
            break;
        }
    }

    p_firstBlock = firstBlock.blockNumber();
    p_lastBlock = lastBlock.blockNumber();
    if (p_lastBlock < p_firstBlock) {
        p_lastBlock = p_firstBlock;
    } else if (p_lastBlock - p_firstBlock + 1 > maxNumOfBlocks) {
        p_firstBlock = p_lastBlock = -1;
    }
}

void PegMarkdownHighlighter::rehighlightSensitiveBlocks()
{
    QTextBlock cb = m_editor->textCursorW().block();

    int first, last;
    m_editor->visibleBlockRange(first, last);

    bool cursorVisible = cb.blockNumber() >= first && cb.blockNumber() <= last;

    // Include extra blocks.
    const int nrUpExtra = 5;
    const int nrDownExtra = 20;
    first = qMax(0, first - nrUpExtra);
    last = qMin(m_doc->blockCount() - 1, last + nrDownExtra);

    if (rehighlightBlockRange(first, last)) {
        if (cursorVisible) {
            m_editor->ensureCursorVisibleW();
        }
    }
}

void PegMarkdownHighlighter::rehighlightBlocks()
{
    if (m_result->m_numOfBlocks <= LARGE_BLOCK_NUMBER) {
        rehighlightBlockRange(0, m_result->m_numOfBlocks - 1);
    } else {
        rehighlightSensitiveBlocks();
    }
}

bool PegMarkdownHighlighter::rehighlightBlockRange(int p_first, int p_last)
{
    bool highlighted = false;
    const QHash<int, HighlightBlockState> &cbStates = m_result->m_codeBlocksState;

    QTextBlock block = m_doc->findBlockByNumber(p_first);
    while (block.isValid()) {
        int blockNum = block.blockNumber();
        if (blockNum > p_last) {
            break;
        }

        bool needHL = PegMarkdownHighlighter::blockTimeStamp(block) != m_result->m_timeStamp;
        if (!needHL) {
            auto it = cbStates.find(blockNum);
            if (it != cbStates.end()
                && it.value() == HighlightBlockState::CodeBlock
                && PegMarkdownHighlighter::blockCodeBlockTimeStamp(block) != m_result->m_codeBlockTimeStamp) {
                needHL = true;
            }
        }

        if (needHL) {
            highlighted = true;
            rehighlightBlock(block);
        }

        block = block.next();
    }

    return highlighted;
}
