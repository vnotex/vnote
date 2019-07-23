#include "vcodeblockhighlighthelper.h"

#include <QDebug>
#include <QStringList>

#include "vdocument.h"
#include "utils/vutils.h"
#include "utils/veditutils.h"
#include "pegmarkdownhighlighter.h"
#include "vconfigmanager.h"
#include "utils/vprocessutils.h"

extern VConfigManager *g_config;

#define TaskIdProperty "PygmentTaskId"
#define TaskTimeStampProperty "PygmentTaskTimeStamp"

VCodeBlockHighlightHelper::VCodeBlockHighlightHelper(PegMarkdownHighlighter *p_highlighter,
                                                     MarkdownConverterType p_type)
    : QObject(p_highlighter),
      m_highlighter(p_highlighter),
      m_type(p_type),
      m_timeStamp(0)
{
    m_program = g_config->getPygmentPrg();
    m_args << "-f" << "html" << "-O" << "encoding=utf8";

    qDebug() << "code block highlight helper" << m_program << m_args;

    connect(m_highlighter, &PegMarkdownHighlighter::codeBlocksUpdated,
            this, &VCodeBlockHighlightHelper::handleCodeBlocksUpdated);
}

QString VCodeBlockHighlightHelper::unindentCodeBlock(const QString &p_text)
{
    if (p_text.isEmpty()) {
        return p_text;
    }

    QStringList lines = p_text.split('\n');
    Q_ASSERT(lines[0].trimmed().startsWith("```") || lines[0].trimmed().startsWith("~~~"));
    Q_ASSERT(lines.size() > 1);

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

void VCodeBlockHighlightHelper::handleCodeBlocksUpdated(TimeStamp p_timeStamp,
                                                        const QVector<VCodeBlock> &p_codeBlocks)
{
    m_timeStamp = p_timeStamp;
    m_codeBlocks = p_codeBlocks;
    for (int i = 0; i < m_codeBlocks.size(); ++i) {
        const VCodeBlock &block = m_codeBlocks[i];
        auto it = m_cache.find(block.m_text);
        if (it != m_cache.end()) {
            // Hit cache.
            qDebug() << "code block highlight hit cache" << p_timeStamp << i;
            it.value().m_timeStamp = p_timeStamp;
            updateHighlightResults(p_timeStamp, block.m_startPos, it.value().m_units);
        } else {
            QString unindentedText = unindentCodeBlock(block.m_text);
            highlightTextAsync(VEditUtils::removeCodeBlockFence(unindentedText),
                               block.m_lang,
                               i,
                               p_timeStamp);
        }
    }
}

void VCodeBlockHighlightHelper::highlightTextAsync(const QString &p_text,
                                                   const QString &p_lang,
                                                   int p_id,
                                                   unsigned long long p_timeStamp)
{
    if (p_lang.isEmpty() || m_program.isEmpty()) {
        updateHighlightResults(p_timeStamp, 0, QVector<HLUnitPos>());
        return;
    }

    QProcess *process = new QProcess(this);
    process->setProperty(TaskIdProperty, p_id);
    process->setProperty(TaskTimeStampProperty, p_timeStamp);
    connect(process, SIGNAL(finished(int, QProcess::ExitStatus)),
            this, SLOT(handleProcessFinished(int, QProcess::ExitStatus)));

    QStringList args(m_args);
    args << "-l" << p_lang;
    process->start(m_program, args);

    if (process->write(p_text.toUtf8()) == -1) {
        qWarning() << "fail to write to QProcess:" << process->errorString();
    }

    process->closeWriteChannel();
}

void VCodeBlockHighlightHelper::handleTextHighlightResult(const QString &p_html,
                                                          int p_id,
                                                          unsigned long long p_timeStamp)
{
    // Abandon obsolete result.
    if (m_timeStamp != p_timeStamp) {
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

    // Remove the leading spaces.
    int nonSpaceIdx = 0;
    while (nonSpaceIdx < regStr.size() && regStr[nonSpaceIdx].isSpace()) {
        ++nonSpaceIdx;
    }

    if (nonSpaceIdx > 0 && nonSpaceIdx < regStr.size()) {
        regStr.remove(0, nonSpaceIdx);
    }

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
void VCodeBlockHighlightHelper::parseHighlightResult(TimeStamp p_timeStamp,
                                                     int p_idx,
                                                     const QString &p_html)
{
    const VCodeBlock &block = m_codeBlocks.at(p_idx);
    int startPos = block.m_startPos;
    QString text = block.m_text;

    QVector<HLUnitPos> hlUnits;

    bool failed = true;

    QXmlStreamReader xml(p_html);

    if (p_html.isEmpty()) {
        goto exit;
    }

    {
    // Must have a fenced line at the front.
    // textIndex is the start index in the code block text to search for.
    int textIndex = text.indexOf('\n');
    if (textIndex == -1) {
        goto exit;
    }
    ++textIndex;

    if (xml.readNextStartElement()) {
        if (xml.name() != "div") {
            goto exit;
        }

        if (!xml.readNextStartElement()) {
            goto exit;
        }

        if (xml.name() != "pre") {
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
    }

exit:
    // Pass result back to highlighter.
    // Abandon obsolete result.
    if (m_timeStamp != p_timeStamp) {
        return;
    }

    if (xml.hasError() || failed) {
        qWarning() << "fail to parse highlighted result"
                   << "stamp:" << p_timeStamp << "index:" << p_idx << p_html;
        hlUnits.clear();
    }

    // Add it to cache.
    addToHighlightCache(text, p_timeStamp, hlUnits);

    updateHighlightResults(p_timeStamp, startPos, hlUnits);
}

void VCodeBlockHighlightHelper::updateHighlightResults(TimeStamp p_timeStamp,
                                                       int p_startPos,
                                                       QVector<HLUnitPos> p_units)
{
    for (int i = 0; i < p_units.size(); ++i) {
        p_units[i].m_position += p_startPos;
    }

    // We need to call this function anyway to trigger the rehighlight.
    m_highlighter->setCodeBlockHighlights(p_timeStamp, p_units);
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
            if (p_index > unitStart) {
                HLUnitPos unit(unitStart, p_index - unitStart, style);
                p_units.append(unit);
            }

            return true;
        } else {
            return false;
        }
    }
    return false;
}

void VCodeBlockHighlightHelper::addToHighlightCache(const QString &p_text,
                                                    TimeStamp p_timeStamp,
                                                    const QVector<HLUnitPos> &p_units)
{
    const int c_maxEntries = 100;
    const TimeStamp c_maxTimeStampSpan = 3;
    if (m_cache.size() >= c_maxEntries) {
        // Remove the oldest one.
        TimeStamp ts = p_timeStamp - c_maxTimeStampSpan;
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

void VCodeBlockHighlightHelper::handleProcessFinished(int p_exitCode, QProcess::ExitStatus p_exitStatus)
{
    QProcess *process = static_cast<QProcess *>(sender());
    int id = process->property(TaskIdProperty).toInt();
    TimeStamp timeStamp = process->property(TaskTimeStampProperty).toULongLong();
    qDebug() << QString("Pygment finished: id %1 timestamp %2 exitcode %3 exitstatus %4")
                       .arg(id)
                       .arg(timeStamp)
                       .arg(p_exitCode)
                       .arg(p_exitStatus);
    bool failed = true;
    if (p_exitStatus == QProcess::NormalExit) {
        if (p_exitCode < 0) {
            qWarning() << "Pygment fail" << p_exitCode;
        } else {
            failed = false;
            QByteArray outBa = process->readAllStandardOutput();
            handleTextHighlightResult(QString::fromUtf8(outBa), id, timeStamp);
        }
    } else {
        qWarning() << "fail to start Pygment process" << p_exitCode << p_exitStatus;
    }

    QByteArray errBa = process->readAllStandardError();
    if (!errBa.isEmpty()) {
        QString errStr(QString::fromLocal8Bit(errBa));
        if (failed) {
            qWarning() << "Pygment stderr:" << errStr;
        } else {
            qDebug() << "Pygment stderr:" << errStr;
        }
    }

    if (failed) {
        handleTextHighlightResult(QString(), id, timeStamp);
    }

    process->deleteLater();
}
