#ifndef VCODEBLOCKHIGHLIGHTHELPER_H
#define VCODEBLOCKHIGHLIGHTHELPER_H

#include <QObject>
#include <QList>
#include <QAtomicInteger>
#include <QXmlStreamReader>
#include "vconfigmanager.h"

class VDocument;

class VCodeBlockHighlightHelper : public QObject
{
    Q_OBJECT
public:
    VCodeBlockHighlightHelper(HGMarkdownHighlighter *p_highlighter,
                              VDocument *p_vdoc, MarkdownConverterType p_type);

signals:

private slots:
    void handleCodeBlocksUpdated(const QList<VCodeBlock> &p_codeBlocks);
    void handleTextHighlightResult(const QString &p_html, int p_id, int p_timeStamp);

private:
    void parseHighlightResult(int p_timeStamp, int p_idx, const QString &p_html);

    // @p_startPos: the global position of the start of the code block;
    // @p_text: the raw text of the code block;
    // @p_index: the start index of the span element within @p_text;
    // @p_units: all the highlight units of this code block;
    bool parseSpanElement(QXmlStreamReader &p_xml, int p_startPos,
                          const QString &p_text, int &p_index,
                          QList<HLUnitPos> &p_units);
    // @p_text: text of fenced code block.
    // Get the indent level of the first line (fence) and unindent the whole block
    // to make the fence at the highest indent level.
    // This operation is to make sure JS could handle the code block correctly
    // without any context.
    QString unindentCodeBlock(const QString &p_text);

    HGMarkdownHighlighter *m_highlighter;
    VDocument *m_vdocument;
    MarkdownConverterType m_type;
    QAtomicInteger<int> m_timeStamp;
    QList<VCodeBlock> m_codeBlocks;
};

#endif // VCODEBLOCKHIGHLIGHTHELPER_H
