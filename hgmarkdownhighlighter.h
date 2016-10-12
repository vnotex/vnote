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
#include <QThread>

extern "C" {
#include "utils/peg-highlight/pmh_parser.h"
}

QT_BEGIN_NAMESPACE
class QTextDocument;
QT_END_NAMESPACE

class WorkerThread : public QThread
{
public:
    WorkerThread();
    ~WorkerThread();
    void prepareAndStart(const char *data);
    pmh_element** retriveResult();

protected:
    void run();

private:
    void resizeBuffer(int newCap);

    char *content;
    int capacity;
    pmh_element **result;
    static const int initCapacity;
};

struct HighlightingStyle
{
    pmh_element_type type;
    QTextCharFormat format;
};

class HGMarkdownHighlighter : public QObject
{
    Q_OBJECT

public:
    HGMarkdownHighlighter(QTextDocument *parent = 0, int aWaitInterval = 2000);
    ~HGMarkdownHighlighter();
    void setStyles(const QVector<HighlightingStyle> &styles);
    int waitInterval;

private slots:
    void handleContentsChange(int position, int charsRemoved, int charsAdded);
    void threadFinished();
    void timerTimeout();

private:
    QTimer *timer;
    QTextDocument *document;
    WorkerThread *workerThread;
    bool parsePending;
    pmh_element **cached_elements;
    QVector<HighlightingStyle> highlightingStyles;

    void clearFormatting();
    void highlight();
    void parse();
    void setDefaultStyles();
};

#endif
