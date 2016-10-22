/* PEG Markdown Highlight
 * Copyright 2011-2016 Ali Rantakari -- http://hasseg.org
 * Licensed under the GPL2+ and MIT licenses (see LICENSE for more info).
 * 
 * highlighter.h
 * 
 * Qt 4.7 example for highlighting a rich text widget.
 */

#ifndef HGMARKDOWNHIGHLIGHTER_H
#define HGMARKDOWNHIGHLIGHTER_H

#include <QTextCharFormat>
#include <QSyntaxHighlighter>
#include <QAtomicInt>

extern "C" {
#include "utils/peg-highlight/pmh_parser.h"
}

QT_BEGIN_NAMESPACE
class QTextDocument;
QT_END_NAMESPACE

struct HighlightingStyle
{
    pmh_element_type type;
    QTextCharFormat format;
};

// One continuous region for a certain markdown highlight style
// within a QTextBlock.
// Pay attention to the change of HighlightingStyles[]
struct HLUnit
{
    // Highlight offset @start and @length with style HighlightingStyles[styleIndex]
    // within a QTextBlock
    unsigned long start;
    unsigned long length;
    unsigned int styleIndex;
};

class HGMarkdownHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    HGMarkdownHighlighter(const QVector<HighlightingStyle> &styles, int waitInterval,
                          QTextDocument *parent = 0);
    ~HGMarkdownHighlighter();
    void setStyles(const QVector<HighlightingStyle> &styles);

protected:
    void highlightBlock(const QString &text) Q_DECL_OVERRIDE;

private slots:
    void handleContentChange(int position, int charsRemoved, int charsAdded);
    void timerTimeout();

private:
    QRegExp codeBlockStartExp;
    QRegExp codeBlockEndExp;
    QTextCharFormat codeBlockFormat;

    QTextDocument *document;
    QVector<HighlightingStyle> highlightingStyles;
    QVector<QVector<HLUnit> > blockHighlights;
    QAtomicInt parsing;
    QTimer *timer;
    int waitInterval;

    char *content;
    int capacity;
    pmh_element **result;
    static const int initCapacity;
    void resizeBuffer(int newCap);

    void highlightCodeBlock(const QString &text);
    void parse();
    void parseInternal();
    void initBlockHighlightFromResult(int nrBlocks);
    void initBlockHighlihgtOne(unsigned long pos, unsigned long end,
                               int styleIndex);
};

#endif
