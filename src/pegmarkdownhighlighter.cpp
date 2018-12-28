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

#define LARGE_BLOCK_NUMBER 1000

PegMarkdownHighlighter::PegMarkdownHighlighter(QTextDocument *p_doc, VMdEditor *p_editor)
    : QSyntaxHighlighter(p_doc),
      m_doc(p_doc),
      m_editor(p_editor),
      m_timeStamp(0),
      m_codeBlockTimeStamp(0),
      m_parser(nullptr),
      m_parserExts(pmh_EXT_NOTES
                   | pmh_EXT_STRIKE
                   | pmh_EXT_FRONTMATTER
                   | pmh_EXT_MARK
                   | pmh_EXT_TABLE),
      m_parseInterval(50),
      m_notifyHighlightComplete(false),
      m_fastParseInterval(30)
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
        m_parserExts |= (pmh_EXT_MATH | pmh_EXT_MATH_RAW);
    }

    m_parseInterval = p_timerInterval;

    m_codeBlockFormat.setForeground(QBrush(Qt::darkYellow));
    for (auto &m_style : m_styles) {
        switch (m_style.type) {
        case pmh_FENCEDCODEBLOCK:
            m_codeBlockFormat = m_style.format;
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

    m_fastParseBlocks = QPair<int, int>(-1, -1);

    m_parser = new PegParser(this);
    connect(m_parser, &PegParser::parseResultReady,
            this, &PegMarkdownHighlighter::handleParseResult);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(m_parseInterval);
    connect(m_timer, &QTimer::timeout,
            this, &PegMarkdownHighlighter::startParse);

    m_fastParseTimer = new QTimer(this);
    m_fastParseTimer->setSingleShot(true);
    m_fastParseTimer->setInterval(m_fastParseInterval);
    connect(m_fastParseTimer, &QTimer::timeout,
            this, [this]() {
                startFastParse(m_fastParseInfo.m_position,
                               m_fastParseInfo.m_charsRemoved,
                               m_fastParseInfo.m_charsAdded);
            });

    m_scrollRehighlightTimer = new QTimer(this);
    m_scrollRehighlightTimer->setSingleShot(true);
    m_scrollRehighlightTimer->setInterval(5);
    connect(m_scrollRehighlightTimer, &QTimer::timeout,
            this, [this]() {
                if (m_result->m_numOfBlocks > LARGE_BLOCK_NUMBER) {
                    rehighlightSensitiveBlocks();
                }
            });

    m_rehighlightTimer = new QTimer(this);
    m_rehighlightTimer->setSingleShot(true);
    m_rehighlightTimer->setInterval(10);
    connect(m_rehighlightTimer, &QTimer::timeout,
            this, &PegMarkdownHighlighter::rehighlightBlocks);

    connect(m_doc, &QTextDocument::contentsChange,
            this, &PegMarkdownHighlighter::handleContentsChange);

    connect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged,
            m_scrollRehighlightTimer, static_cast<void(QTimer::*)()>(&QTimer::start));

    m_contentChangeTime.start();
}

// Just use parse results to highlight block.
// Do not maintain block data and state here.
void PegMarkdownHighlighter::highlightBlock(const QString &p_text)
{
    QSharedPointer<PegHighlighterResult> result(m_result);

    QTextBlock block = currentBlock();
    int blockNum = block.blockNumber();

    bool isCodeBlock = currentBlockState() == HighlightBlockState::CodeBlock;
    bool isNewBlock = block.userData() == nullptr;
    VTextBlockData *blockData = VTextBlockData::blockData(block);
    QVector<HLUnit> *cache = &blockData->getBlockHighlightCache();

    // Fast parse can not cross multiple empty lines in code block, which
    // cause the wrong parse results.
    if (isNewBlock) {
        int pstate = previousBlockState();
        if (pstate == HighlightBlockState::CodeBlock
            || pstate == HighlightBlockState::CodeBlockStart) {
            setCurrentBlockState(HighlightBlockState::CodeBlock);
            isCodeBlock = true;
        }
    }

    bool cacheValid = true;
    if (result->matched(m_timeStamp)) {
        if (preHighlightSingleFormatBlock(result->m_blocksHighlights,
                                          blockNum,
                                          p_text,
                                          isCodeBlock)) {
            cacheValid = false;
        } else if (blockData->isCacheValid() && blockData->getTimeStamp() == m_timeStamp) {
            // Use the cache to highlight.
            highlightBlockOne(*cache);
        } else {
            cache->clear();
            highlightBlockOne(result->m_blocksHighlights, blockNum, cache);
        }
    } else {
        // If fast result cover this block, we do not need to use the outdated one.
        if (isFastParseBlock(blockNum)) {
            if (!preHighlightSingleFormatBlock(m_fastResult->m_blocksHighlights,
                                               blockNum,
                                               p_text,
                                               isCodeBlock)) {
                highlightBlockOne(m_fastResult->m_blocksHighlights, blockNum, nullptr);
            }

            cacheValid = false;
        } else {
            if (preHighlightSingleFormatBlock(result->m_blocksHighlights,
                                              blockNum,
                                              p_text,
                                              isCodeBlock)) {
                cacheValid = false;
            } else if (blockData->isCacheValid() && result->matched(blockData->getTimeStamp())) {
                // Use the cache to highlight.
                highlightBlockOne(*cache);
            } else {
                cache->clear();
                highlightBlockOne(result->m_blocksHighlights, blockNum, cache);
            }
        }
    }

    blockData->setCacheValid(cacheValid);
    PegMarkdownHighlighter::updateBlockTimeStamp(block, result->m_timeStamp);

    if (isCodeBlock) {
        QVector<HLUnitStyle> *cbCache = &blockData->getCodeBlockHighlightCache();
        if (blockData->getCodeBlockTimeStamp() == result->m_codeBlockTimeStamp
            || !result->m_codeBlockHighlightReceived) {
            highlightCodeBlock(*cbCache, p_text);
        } else {
            cbCache->clear();
            highlightCodeBlock(result, blockNum, p_text, cbCache);
            PegMarkdownHighlighter::updateBlockCodeBlockTimeStamp(block, result->m_codeBlockTimeStamp);
        }

        highlightCodeBlockColorColumn(p_text);
    }
}

inline
static bool containSpecialChar(const QString &p_str)
{
    QChar fi = p_str[0];
    QChar la = p_str[p_str.size() - 1];

    return fi == '#'
           || la == '`' || la == '$' || la == '~' || la == '*' || la == '_';
}

bool PegMarkdownHighlighter::preHighlightSingleFormatBlock(const QVector<QVector<HLUnit>> &p_highlights,
                                                           int p_blockNum,
                                                           const QString &p_text,
                                                           bool p_forced)
{
    int sz = p_text.size();
    if (sz == 0) {
        return false;
    }

    if (p_highlights.size() <= p_blockNum) {
        return false;
    }

    if (!p_forced && !m_singleFormatBlocks.contains(p_blockNum)) {
        return false;
    }

    const QVector<HLUnit> &units = p_highlights[p_blockNum];
    if (units.size() == 1) {
        const HLUnit &unit = units[0];
        if (unit.start == 0
            && (int)unit.length < sz
            && (p_forced || containSpecialChar(p_text))) {
            setFormat(0, sz, m_styles[unit.styleIndex].format);
            return true;
        }
    }

    return false;
}

bool PegMarkdownHighlighter::highlightBlockOne(const QVector<QVector<HLUnit>> &p_highlights,
                                               int p_blockNum,
                                               QVector<HLUnit> *p_cache)
{
    bool highlighted = false;
    if (p_highlights.size() > p_blockNum) {
        // units are sorted by start position and length.
        const QVector<HLUnit> &units = p_highlights[p_blockNum];
        if (!units.isEmpty()) {
            highlighted = true;
            if (p_cache) {
                p_cache->append(units);
            }

            highlightBlockOne(units);
        }
    }

    return highlighted;
}

void PegMarkdownHighlighter::highlightBlockOne(const QVector<HLUnit> &p_units)
{
    for (int i = 0; i < p_units.size(); ++i) {
        const HLUnit &unit = p_units[i];
        if (i == 0) {
            // No need to merge format.
            setFormat(unit.start,
                      unit.length,
                      m_styles[unit.styleIndex].format);
        } else {
            QTextCharFormat newFormat = m_styles[unit.styleIndex].format;
            for (int j = i - 1; j >= 0; --j) {
                if (p_units[j].start + p_units[j].length <= unit.start) {
                    // It won't affect current unit.
                    continue;
                } else {
                    // Merge the format.
                    QTextCharFormat tmpFormat(newFormat);
                    newFormat = m_styles[p_units[j].styleIndex].format;
                    // tmpFormat takes precedence.
                    newFormat.merge(tmpFormat);
                }
            }

            setFormat(unit.start, unit.length, newFormat);
        }
    }
}

#define KEY_PRESS_INTERVAL 50

// highlightBlock() will be called before this function.
void PegMarkdownHighlighter::handleContentsChange(int p_position, int p_charsRemoved, int p_charsAdded)
{
    Q_UNUSED(p_position);

    int interval = m_contentChangeTime.restart();

    if (p_charsRemoved == 0 && p_charsAdded == 0) {
        return;
    }

    ++m_timeStamp;

    m_timer->stop();

    if (m_timeStamp > 2) {
        m_fastParseInfo.m_position = p_position;
        m_fastParseInfo.m_charsRemoved = p_charsRemoved;
        m_fastParseInfo.m_charsAdded = p_charsAdded;
        m_fastParseTimer->start(interval < KEY_PRESS_INTERVAL ? 100 : m_fastParseInterval);
    }

    // We still need a timer to start a complete parse.
    m_timer->start(m_timeStamp == 2 ? 0 : m_parseInterval);
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
        clearFastParseResult();
        m_fastParseInterval = 100;
        return;
    } else {
        m_fastParseInterval = (lastBlockNum - firstBlockNum) < 5 ? 0 : 30;
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

    m_fastParseBlocks.first = firstBlockNum;
    m_fastParseBlocks.second = lastBlockNum;

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
    m_fastResult.reset(new PegHighlighterFastResult(this, p_result));

    // Add additional single format blocks.
    updateSingleFormatBlocks(m_fastResult->m_blocksHighlights);

    if (!m_fastResult->matched(m_timeStamp) || m_result->matched(m_timeStamp)) {
        return;
    }

    for (int i = m_fastParseBlocks.first; i <= m_fastParseBlocks.second; ++i) {
        QTextBlock block = m_doc->findBlockByNumber(i);
        rehighlightBlock(block);
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
        result->m_codeBlockTimeStamp = nextCodeBlockTimeStamp();
        result->m_codeBlockHighlightReceived = true;
        rehighlightBlocksLater();
    }
}

void PegMarkdownHighlighter::updateHighlight()
{
    m_timer->stop();
    if (m_result->matched(m_timeStamp)) {
        // No need to parse again. Already the latest.
        updateCodeBlocks(m_result);
        rehighlightBlocksLater();
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

    clearFastParseResult();

    m_result.reset(new PegHighlighterResult(this, p_result));

    m_result->m_codeBlockTimeStamp = nextCodeBlockTimeStamp();

    m_singleFormatBlocks.clear();
    updateSingleFormatBlocks(m_result->m_blocksHighlights);

    bool matched = m_result->matched(m_timeStamp);
    if (matched) {
        clearAllBlocksUserDataAndState(m_result);

        updateAllBlocksUserState(m_result);

        updateCodeBlocks(m_result);
    }

    if (m_result->m_timeStamp == 2) {
        m_notifyHighlightComplete = true;
        rehighlightBlocks();
    } else {
        rehighlightBlocksLater();
    }

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
    if (g_config->getEnableCodeBlockHighlight()) {
        int cbSz = p_result->m_codeBlocks.size();
        if (cbSz > 0) {
            if (PegMarkdownHighlighter::isEmptyCodeBlockHighlights(p_result->m_codeBlocksHighlights)) {
                p_result->m_codeBlocksHighlights.resize(p_result->m_numOfBlocks);
                p_result->m_numOfCodeBlockHighlightsToRecv = cbSz;
            }
        } else {
            p_result->m_codeBlockHighlightReceived = true;
        }
    } else {
        p_result->m_codeBlockHighlightReceived = true;
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
    VTextBlockData *data = VTextBlockData::blockData(p_block);
    if (!data) {
        return;
    }

    data->setCodeBlockIndentation(-1);
    if (data->getPreviews().isEmpty()) {
        m_possiblePreviewBlocks.remove(blockNum);
    } else {
        m_possiblePreviewBlocks.insert(blockNum);
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
            auto blockData = static_cast<VTextBlockData *>(block.userData());
            Q_ASSERT(blockData);

            switch (it.value()) {
            case HighlightBlockState::CodeBlockStart:
            {
                int startLeadingSpaces = 0;
                QRegularExpression reg(VUtils::c_fencedCodeBlockStartRegExp);
                auto match = reg.match(block.text());
                if (match.hasMatch()) {
                    startLeadingSpaces = match.captured(1).size();
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
                                                const QString &p_text,
                                                QVector<HLUnitStyle> *p_cache)
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
            if (p_cache) {
                p_cache->append(units);
            }

            highlightCodeBlockOne(units);
        }
    }
}

void PegMarkdownHighlighter::highlightCodeBlock(const QVector<HLUnitStyle> &p_units,
                                                const QString &p_text)
{
    // Brush the indentation spaces.
    if (currentBlockState() == HighlightBlockState::CodeBlock) {
        int spaces = VEditUtils::fetchIndentation(p_text);
        if (spaces > 0) {
            setFormat(0, spaces, m_codeBlockFormat);
        }
    }

    if (!p_units.isEmpty()) {
        highlightCodeBlockOne(p_units);
    }
}

void PegMarkdownHighlighter::highlightCodeBlockOne(const QVector<HLUnitStyle> &p_units)
{
    QVector<QTextCharFormat *> formats(p_units.size(), NULL);
    for (int i = 0; i < p_units.size(); ++i) {
        const HLUnitStyle &unit = p_units[i];
        auto it = m_codeBlockStyles.find(unit.style);
        if (it == m_codeBlockStyles.end()) {
            continue;
        }

        formats[i] = &(*it);

        QTextCharFormat newFormat = m_codeBlockFormat;
        newFormat.merge(*it);
        for (int j = i - 1; j >= 0; --j) {
            if (p_units[j].start + p_units[j].length <= unit.start) {
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
    m_notifyHighlightComplete = true;

    if (isMathJaxEnabled()) {
        emit mathjaxBlocksUpdated(p_result->m_mathjaxBlocks);
    }

    emit tableBlocksUpdated(p_result->m_tableBlocks);

    emit imageLinksUpdated(p_result->m_imageRegions);
    emit headersUpdated(p_result->m_headerRegions);
}

void PegMarkdownHighlighter::getFastParseBlockRange(int p_position,
                                                    int p_charsRemoved,
                                                    int p_charsAdded,
                                                    int &p_firstBlock,
                                                    int &p_lastBlock) const
{
    const int maxNumOfBlocks = 15;

    int charsChanged = p_charsRemoved + p_charsAdded;
    QTextBlock firstBlock = m_doc->findBlock(p_position);

    // May be an invalid block.
    QTextBlock lastBlock = m_doc->findBlock(qMax(0, p_position + charsChanged));
    if (!lastBlock.isValid()) {
        lastBlock = m_doc->lastBlock();
    }

    int num = lastBlock.blockNumber() - firstBlock.blockNumber() + 1;
    if (num > maxNumOfBlocks) {
        p_firstBlock = p_lastBlock = -1;
        return;
    }

    // Look up.
    while (firstBlock.isValid() && num <= maxNumOfBlocks) {
        QTextBlock preBlock = firstBlock.previous();
        if (!preBlock.isValid()) {
            break;
        }

        // Check code block.
        int state = firstBlock.userState();
        if (state == HighlightBlockState::CodeBlock
            || state == HighlightBlockState::CodeBlockEnd) {
            goto goup;
        }

        // Empty block.
        if (VEditUtils::isEmptyBlock(firstBlock)) {
            goto goup;
        }

        if (VEditUtils::fetchIndentation(firstBlock) < 4) {
            // If previous block is empty, then we could stop now.
            if (VEditUtils::isEmptyBlock(preBlock)) {
                int preState = preBlock.userState();
                if (preState != HighlightBlockState::CodeBlockStart
                    && preState != HighlightBlockState::CodeBlock) {
                    break;
                }
            }
        }

goup:
        firstBlock = preBlock;
        ++num;
    }

    // Look down.
    bool inCodeBlock = false;
    while (lastBlock.isValid() && num <= maxNumOfBlocks) {
        QTextBlock nextBlock = lastBlock.next();
        if (!nextBlock.isValid()) {
            break;
        }

        // Check code block.
        switch (lastBlock.userState()) {
        case HighlightBlockState::CodeBlockStart:
            V_FALLTHROUGH;
        case HighlightBlockState::CodeBlock:
            inCodeBlock = true;
            goto godown;

        case HighlightBlockState::CodeBlockEnd:
            inCodeBlock = false;
            break;

        default:
            break;
        }

        // Empty block.
        if (VEditUtils::isEmptyBlock(nextBlock) && !inCodeBlock) {
            int nstate = nextBlock.userState();
            if (nstate != HighlightBlockState::CodeBlockStart
                && nstate != HighlightBlockState::CodeBlock
                && nstate != HighlightBlockState::CodeBlockEnd) {
                break;
            }
        }

godown:
        lastBlock = nextBlock;
        ++num;
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

    if (m_notifyHighlightComplete) {
        m_notifyHighlightComplete = false;
        emit highlightCompleted();
    }
}

bool PegMarkdownHighlighter::rehighlightBlockRange(int p_first, int p_last)
{
    bool highlighted = false;
    const QHash<int, HighlightBlockState> &cbStates = m_result->m_codeBlocksState;
    const QVector<QVector<HLUnit>> &hls = m_result->m_blocksHighlights;
    const QVector<QVector<HLUnitStyle>> &cbHls = m_result->m_codeBlocksHighlights;

    int nr = 0;
    QTextBlock block = m_doc->findBlockByNumber(p_first);
    while (block.isValid()) {
        int blockNum = block.blockNumber();
        if (blockNum > p_last) {
            break;
        }

        bool needHL = false;
        bool updateTS = false;
        VTextBlockData *data = VTextBlockData::blockData(block);
        if (PegMarkdownHighlighter::blockTimeStamp(block) != m_result->m_timeStamp) {
            needHL = true;
            // Try to find cache.
            if (blockNum < hls.size()) {
                if (data->isBlockHighlightCacheMatched(hls[blockNum])) {
                    needHL = false;
                    updateTS = true;
                }
            }
        }

        if (!needHL) {
            // FIXME: what about a previous code block turn into a non-code block? For now,
            // they can be distinguished by block highlights.
            auto it = cbStates.find(blockNum);
            if (it != cbStates.end() && it.value() == HighlightBlockState::CodeBlock) {
                if (PegMarkdownHighlighter::blockCodeBlockTimeStamp(block) != m_result->m_codeBlockTimeStamp
                    && m_result->m_codeBlockHighlightReceived) {
                    needHL = true;
                    // Try to find cache.
                    if (blockNum < cbHls.size()) {
                        if (data->isCodeBlockHighlightCacheMatched(cbHls[blockNum])) {
                            needHL = false;
                            updateTS = true;
                        }
                    }
                }
            }
        }

        if (needHL) {
            highlighted = true;
            rehighlightBlock(block);
            ++nr;
        } else if (updateTS) {
            data->setCacheValid(true);
            data->setTimeStamp(m_result->m_timeStamp);
            data->setCodeBlockTimeStamp(m_result->m_codeBlockTimeStamp);
        }

        block = block.next();
    }

    qDebug() << "rehighlightBlockRange" << p_first << p_last << nr;
    return highlighted;
}

void PegMarkdownHighlighter::clearFastParseResult()
{
    m_fastParseBlocks.first = -1;
    m_fastParseBlocks.second = -1;
    m_fastResult->clear();
}

void PegMarkdownHighlighter::rehighlightBlocksLater()
{
    m_rehighlightTimer->start();
}
