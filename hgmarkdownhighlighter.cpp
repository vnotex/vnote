/* PEG Markdown Highlight
 * Copyright 2011-2016 Ali Rantakari -- http://hasseg.org
 * Licensed under the GPL2+ and MIT licenses (see LICENSE for more info).
 * 
 * highlighter.cpp
 * 
 * Qt 4.7 example for highlighting a rich text widget.
 */

#include <QtGui>
#include <QtDebug>
#include "hgmarkdownhighlighter.h"

#ifndef QT_NO_DEBUG
#define V_HIGHLIGHT_DEBUG
#endif

const unsigned int WorkerThread::initCapacity = 1024;

WorkerThread::WorkerThread()
    : QThread(NULL), content(NULL), capacity(0), result(NULL)
{
    resizeBuffer(initCapacity);
}

WorkerThread::~WorkerThread()
{
    if (result) {
        pmh_free_elements(result);
        result = NULL;
    }
    if (content) {
        delete [] content;
        capacity = 0;
        content = NULL;
    }
}

void WorkerThread::resizeBuffer(unsigned int newCap)
{
    if (newCap == capacity) {
        return;
    }
    if (capacity > 0) {
        Q_ASSERT(content);
        delete [] content;
    }
    capacity = newCap;
    content = new char [capacity];
}

void WorkerThread::prepareAndStart(const char *data)
{
    Q_ASSERT(data);
    unsigned int len = strlen(data);
    if (len >= capacity) {
        resizeBuffer(qMax(2 * capacity, len + 1));
    }
    Q_ASSERT(content);
    memcpy(content, data, len);
    content[len] = '\0';

    if (result) {
        pmh_free_elements(result);
        result = NULL;
    }

    start();
}

pmh_element** WorkerThread::retriveResult()
{
    Q_ASSERT(result);
    pmh_element** ret = result;
    result = NULL;
    return ret;
}

void WorkerThread::run()
{
    if (content == NULL)
        return;
    Q_ASSERT(!result);
    pmh_markdown_to_elements(content, pmh_EXT_NONE, &result);
}

// Will be freeed by parent automatically
HGMarkdownHighlighter::HGMarkdownHighlighter(const QVector<HighlightingStyle> &styles,
                                             QTextDocument *parent,
                                             int aWaitInterval) : QObject(parent)
{
    workerThread = new WorkerThread();
    cached_elements = NULL;
    waitInterval = aWaitInterval;
    setStyles(styles);
    timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(aWaitInterval);
    connect(timer, SIGNAL(timeout()), this, SLOT(timerTimeout()));
    document = parent;
    connect(document, SIGNAL(contentsChange(int,int,int)),
            this, SLOT(handleContentsChange(int,int,int)));
    connect(workerThread, SIGNAL(finished()), this, SLOT(threadFinished()));
    this->parse();
}

HGMarkdownHighlighter::~HGMarkdownHighlighter()
{
    if (workerThread) {
        if (workerThread->isRunning()) {
            workerThread->wait();
        }
        delete workerThread;
        workerThread = NULL;
    }
    if (cached_elements) {
        pmh_free_elements(cached_elements);
        cached_elements = NULL;
    }
}

void HGMarkdownHighlighter::setStyles(const QVector<HighlightingStyle> &styles)
{
    this->highlightingStyles = styles;
}

void HGMarkdownHighlighter::clearFormatting()
{
    QTextBlock block = document->firstBlock();
    while (block.isValid()) {
        block.layout()->clearAdditionalFormats();
        block = block.next();
    }
}

void HGMarkdownHighlighter::highlightOneRegion(const HighlightingStyle &style,
                                               unsigned long pos, unsigned long end, bool clearBeforeHighlight)
{
    // "The QTextLayout object can only be modified from the
    // documentChanged implementation of a QAbstractTextDocumentLayout
    // subclass. Any changes applied from the outside cause undefined
    // behavior." -- we are breaking this rule here. There might be
    // a better (more correct) way to do this.
    int startBlockNum = document->findBlock(pos).blockNumber();
    int endBlockNum = document->findBlock(end).blockNumber();
    for (int i = startBlockNum; i <= endBlockNum; ++i)
    {
        QTextBlock block = document->findBlockByNumber(i);

        QTextLayout *layout = block.layout();
        if (clearBeforeHighlight) {
            layout->clearFormats();
        }
        QVector<QTextLayout::FormatRange> list = layout->formats();
        int blockpos = block.position();
        QTextLayout::FormatRange r;
        r.format = style.format;

        if (i == startBlockNum) {
            r.start = pos - blockpos;
            r.length = (startBlockNum == endBlockNum)
                        ? end - pos
                        : block.length() - r.start;
        } else if (i == endBlockNum) {
            r.start = 0;
            r.length = end - blockpos;
        } else {
            r.start = 0;
            r.length = block.length();
        }

        list.append(r);
        layout->setFormats(list);
    }
}

void HGMarkdownHighlighter::highlight()
{
    if (cached_elements == NULL) {
        return;
    }

    if (highlightingStyles.isEmpty()) {
        qWarning() << "error: HighlightingStyles is not set";
        return;
    }

    this->clearFormatting();

    // To make sure content is not changed by highlight operations.
    // May be resource-consuming. Can be removed if no need.
#ifdef V_HIGHLIGHT_DEBUG
    QString oriContent = document->toPlainText();
#endif

    for (int i = 0; i < highlightingStyles.size(); i++)
    {
        const HighlightingStyle &style = highlightingStyles[i];
        pmh_element *elem_cursor = cached_elements[style.type];
        while (elem_cursor != NULL)
        {
            if (elem_cursor->end <= elem_cursor->pos) {
                elem_cursor = elem_cursor->next;
                continue;
            }
            highlightOneRegion(style, elem_cursor->pos, elem_cursor->end);
            elem_cursor = elem_cursor->next;
        }
    }

    highlightCodeBlock();

    document->markContentsDirty(0, document->characterCount());

#ifdef V_HIGHLIGHT_DEBUG
    if (oriContent != document->toPlainText()) {
        qWarning() << "warning: content was changed before and after highlighting";
    }
#endif

}

void HGMarkdownHighlighter::highlightCodeBlock()
{
    QRegExp codeRegStart("^```");
    QRegExp codeRegEnd("^```$");

    HighlightingStyle style;
    int index = 0;
    for (index = 0; index < highlightingStyles.size(); ++index) {
        if (highlightingStyles[index].type == pmh_VERBATIM) {
            style = highlightingStyles[index];
            break;
        }
    }
    if (index == highlightingStyles.size()) {
        style.type = pmh_VERBATIM;
        style.format.setForeground(QBrush(Qt::darkYellow));
    }
    int pos = 0;
    while (true) {
        QTextCursor startCursor = document->find(codeRegStart, pos);
        if (!startCursor.hasSelection()) {
            break;
        }
        pos = startCursor.selectionEnd();
        QTextCursor endCursor = document->find(codeRegEnd, pos);
        if (!endCursor.hasSelection()) {
            break;
        }
        pos = endCursor.selectionEnd();
        highlightOneRegion(style, startCursor.selectionStart(), endCursor.selectionEnd(),
                           true);
    }
}

void HGMarkdownHighlighter::parse()
{
    if (workerThread->isRunning()) {
        parsePending = true;
        return;
    }

    QString content = document->toPlainText();
    QByteArray ba = content.toUtf8();
    parsePending = false;
    workerThread->prepareAndStart((const char *)ba.data());
}

void HGMarkdownHighlighter::threadFinished()
{
    if (parsePending) {
        this->parse();
        return;
    }

    if (cached_elements != NULL) {
        pmh_free_elements(cached_elements);
    }
    cached_elements = workerThread->retriveResult();
    this->highlight();
}

void HGMarkdownHighlighter::handleContentsChange(int position, int charsRemoved,
                                                 int charsAdded)
{
    if (charsRemoved == 0 && charsAdded == 0)
        return;

    timer->stop();
    timer->start();
}

void HGMarkdownHighlighter::timerTimeout()
{
    this->parse();
}
