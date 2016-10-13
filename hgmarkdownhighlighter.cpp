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

const int WorkerThread::initCapacity = 1024;

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

void WorkerThread::resizeBuffer(int newCap)
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
    int len = int(strlen(data));
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

            // "The QTextLayout object can only be modified from the
            // documentChanged implementation of a QAbstractTextDocumentLayout
            // subclass. Any changes applied from the outside cause undefined
            // behavior." -- we are breaking this rule here. There might be
            // a better (more correct) way to do this.

            int startBlockNum = document->findBlock(elem_cursor->pos).blockNumber();
            int endBlockNum = document->findBlock(elem_cursor->end).blockNumber();
            for (int j = startBlockNum; j <= endBlockNum; j++)
            {
                QTextBlock block = document->findBlockByNumber(j);

                QTextLayout *layout = block.layout();
                QList<QTextLayout::FormatRange> list = layout->additionalFormats();
                int blockpos = block.position();
                QTextLayout::FormatRange r;
                r.format = style.format;

                if (j == startBlockNum) {
                    r.start = elem_cursor->pos - blockpos;
                    r.length = (startBlockNum == endBlockNum)
                                ? elem_cursor->end - elem_cursor->pos
                                : block.length() - r.start;
                } else if (j == endBlockNum) {
                    r.start = 0;
                    r.length = elem_cursor->end - blockpos;
                } else {
                    r.start = 0;
                    r.length = block.length();
                }

                list.append(r);
                layout->setAdditionalFormats(list);
            }

            elem_cursor = elem_cursor->next;
        }
    }

    document->markContentsDirty(0, document->characterCount());

#ifdef V_HIGHLIGHT_DEBUG
    if (oriContent != document->toPlainText()) {
        qWarning() << "warning: content was changed before and after highlighting";
    }
#endif

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

    position;
    timer->stop();
    timer->start();
}

void HGMarkdownHighlighter::timerTimeout()
{
    this->parse();
}
