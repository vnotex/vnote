#include "vcodeblockhighlighthelper.h"

#include <QDebug>
#include <QStringList>
#include "vdocument.h"
#include "utils/vutils.h"

VCodeBlockHighlightHelper::VCodeBlockHighlightHelper(HGMarkdownHighlighter *p_highlighter,
                                                     VDocument *p_vdoc,
                                                     MarkdownConverterType p_type)
    : QObject(p_highlighter), m_highlighter(p_highlighter), m_vdocument(p_vdoc),
      m_type(p_type), m_timeStamp(0)
{
    connect(m_highlighter, &HGMarkdownHighlighter::codeBlocksUpdated,
            this, &VCodeBlockHighlightHelper::handleCodeBlocksUpdated);
    connect(m_vdocument, &VDocument::textHighlighted,
            this, &VCodeBlockHighlightHelper::handleTextHighlightResult);

    // Web side is ready for code block highlight.
    connect(m_vdocument, &VDocument::readyToHighlightText,
            m_highlighter, &HGMarkdownHighlighter::updateHighlight);
}

QString VCodeBlockHighlightHelper::unindentCodeBlock(const QString &p_text)
{
    if (p_text.isEmpty()) {
        return p_text;
    }

    QStringList lines = p_text.split('\n');
    V_ASSERT(lines[0].trimmed().startsWith("```"));
    V_ASSERT(lines.size() > 1);

    QRegExp regExp("(^\\s*)");
    regExp.indexIn(lines[0]);
    V_ASSERT(regExp.captureCount() == 1);
    int nrSpaces = regExp.capturedTexts()[1].size();

    if (nrSpaces == 0) {
        return p_text;
    }

    QString res = lines[0].right(lines[0].size() - nrSpaces);
    for (int i = 1; i < lines.size(); ++i) {
        const QString &line = lines[i];

        int idx = 0;
        while (idx < nrSpaces && idx < line.size() && line[idx].isSpace()) {
            ++idx;
        }
        res = res + "\n" + line.right(line.size() - idx);
    }

    return res;
}

void VCodeBlockHighlightHelper::handleCodeBlocksUpdated(const QVector<VCodeBlock> &p_codeBlocks)
{
    int curStamp = m_timeStamp.fetchAndAddRelaxed(1) + 1;
    m_codeBlocks = p_codeBlocks;
    for (int i = 0; i < m_codeBlocks.size(); ++i) {
        const VCodeBlock &block = m_codeBlocks[i];
        auto it = m_cache.find(block.m_text);
        if (it != m_cache.end()) {
            // Hit cache.
            qDebug() << "code block highlight hit cache" << curStamp << i;
            it.value().m_timeStamp = curStamp;
            updateHighlightResults(block.m_startPos, it.value().m_units);
        } else {
            QString unindentedText = unindentCodeBlock(block.m_text);
            m_vdocument->highlightTextAsync(unindentedText, i, curStamp);
        }
    }
}

void VCodeBlockHighlightHelper::handleTextHighlightResult(const QString &p_html,
                                                          int p_id,
                                                          int p_timeStamp)
{
    int curStamp = m_timeStamp.load();
    // Abandon obsolete result.
    if (curStamp != p_timeStamp) {
        return;
    }
    parseHighlightResult(p_timeStamp, p_id, p_html);
}

static void revertEscapedHtml(QString &p_html)
{
    p_html.replace("&gt;", ">").replace("&lt;", "<").replace("&amp;", "&");
}

// Search @p_tokenStr in @p_text from p_index. Spaces after `\n` will not make
// a difference in the match. The matched range will be returned as
// [@p_start, @p_end]. Update @p_index to @p_end + 1.
// Set @p_start and @p_end to -1 to indicate mismatch.
static void matchTokenRelaxed(const QString &p_text, const QString &p_tokenStr,
                              int &p_index, int &p_start, int &p_end)
{
    QString regStr = QRegExp::escape(p_tokenStr);
    // Do not replace the ending '\n'.
    regStr.replace(QRegExp("\n(?!$)"), "\\s+");
    QRegExp regExp(regStr);
    p_start = p_text.indexOf(regExp, p_index);
    if (p_start == -1) {
        p_end = -1;
        return;
    }

    p_end = p_start + regExp.matchedLength() - 1;
    p_index = p_end + 1;
}

// For now, we could only handle code blocks outside the list.
void VCodeBlockHighlightHelper::parseHighlightResult(int p_timeStamp,
                                                     int p_idx,
                                                     const QString &p_html)
{
    const VCodeBlock &block = m_codeBlocks.at(p_idx);
    int startPos = block.m_startPos;
    QString text = block.m_text;

    QVector<HLUnitPos> hlUnits;

    bool failed = true;

    QXmlStreamReader xml(p_html);

    // Must have a fenced line at the front.
    // textIndex is the start index in the code block text to search for.
    int textIndex = text.indexOf('\n');
    if (textIndex == -1) {
        goto exit;
    }
    ++textIndex;

    if (xml.readNextStartElement()) {
        if (xml.name() != "pre") {
            goto exit;
        }

        if (!xml.readNextStartElement()) {
            goto exit;
        }

        if (xml.name() != "code") {
            goto exit;
        }

        while (xml.readNext()) {
            if (xml.isCharacters()) {
                // Revert the HTML escape to match.
                QString tokenStr = xml.text().toString();
                revertEscapedHtml(tokenStr);

                int start, end;
                matchTokenRelaxed(text, tokenStr, textIndex, start, end);
                if (start == -1) {
                    failed = true;
                    goto exit;
                }
            } else if (xml.isStartElement()) {
                if (xml.name() != "span") {
                    failed = true;
                    goto exit;
                }
                if (!parseSpanElement(xml, text, textIndex, hlUnits)) {
                    failed = true;
                    goto exit;
                }
            } else if (xml.isEndElement()) {
                if (xml.name() != "code" && xml.name() != "pre") {
                    failed = true;
                } else {
                    failed = false;
                }
                goto exit;
            } else {
                failed = true;
                goto exit;
            }
        }
    }

exit:
    // Pass result back to highlighter.
    int curStamp = m_timeStamp.load();
    // Abandon obsolete result.
    if (curStamp != p_timeStamp) {
        return;
    }

    if (xml.hasError() || failed) {
        qWarning() << "fail to parse highlighted result"
                   << "stamp:" << p_timeStamp << "index:" << p_idx << p_html;
        hlUnits.clear();
    }

    // Add it to cache.
    addToHighlightCache(text, p_timeStamp, hlUnits);

    updateHighlightResults(startPos, hlUnits);
}

void VCodeBlockHighlightHelper::updateHighlightResults(int p_startPos,
                                                       QVector<HLUnitPos> p_units)
{
    for (int i = 0; i < p_units.size(); ++i) {
        p_units[i].m_position += p_startPos;
    }

    // We need to call this function anyway to trigger the rehighlight.
    m_highlighter->setCodeBlockHighlights(p_units);
}

bool VCodeBlockHighlightHelper::parseSpanElement(QXmlStreamReader &p_xml,
                                                 const QString &p_text,
                                                 int &p_index,
                                                 QVector<HLUnitPos> &p_units)
{
    int unitStart = p_index;
    QString style = p_xml.attributes().value("class").toString();

    while (p_xml.readNext()) {
        if (p_xml.isCharacters()) {
            // Revert the HTML escape to match.
            QString tokenStr = p_xml.text().toString();
            revertEscapedHtml(tokenStr);

            int start, end;
            matchTokenRelaxed(p_text, tokenStr, p_index, start, end);
            if (start == -1) {
                return false;
            }
        } else if (p_xml.isStartElement()) {
            if (p_xml.name() != "span") {
                return false;
            }

            // Sub-span.
            if (!parseSpanElement(p_xml, p_text, p_index, p_units)) {
                return false;
            }
        } else if (p_xml.isEndElement()) {
            if (p_xml.name() != "span") {
                return false;
            }

            // Got a complete span. Use relative position here.
            HLUnitPos unit(unitStart, p_index - unitStart, style);
            p_units.append(unit);
            return true;
        } else {
            return false;
        }
    }
    return false;
}

void VCodeBlockHighlightHelper::addToHighlightCache(const QString &p_text,
                                                    int p_timeStamp,
                                                    const QVector<HLUnitPos> &p_units)
{
    const int c_maxEntries = 100;
    const int c_maxTimeStampSpan = 3;
    if (m_cache.size() >= c_maxEntries) {
        // Remove the oldest one.
        int ts = p_timeStamp - c_maxTimeStampSpan;
        for (auto it = m_cache.begin(); it != m_cache.end();) {
            if (it.value().m_timeStamp < ts) {
                it = m_cache.erase(it);
            } else {
                ++it;
            }
        }
    }

    m_cache.insert(p_text, HLResult(p_timeStamp, p_units));
}
