#include "filesearchengine.h"

#include <QFile>
#include <QMimeDatabase>
#include <QDebug>

#include "searchresultitem.h"

using namespace vnotex;

FileSearchEngineWorker::FileSearchEngineWorker(QObject *p_parent)
    : QThread(p_parent)
{
}

void FileSearchEngineWorker::setData(const QVector<SearchSecondPhaseItem> &p_items,
                                     const QSharedPointer<SearchOption> &p_option,
                                     const SearchToken &p_token)
{
    m_items = p_items;
    m_option = p_option;
    m_token = p_token;
}

void FileSearchEngineWorker::stop()
{
    m_askedToStop.store(1);
}

bool FileSearchEngineWorker::isAskedToStop() const
{
    return m_askedToStop.load() == 1;
}

void FileSearchEngineWorker::run()
{
    const int c_batchSize = 100;

    QMimeDatabase mimeDatabase;
    m_state = SearchState::Busy;

    m_results.clear();
    int nr = 0;
    for (const auto &item : m_items) {
        if (isAskedToStop()) {
            m_state = SearchState::Stopped;
            break;
        }

        const QMimeType mimeType = mimeDatabase.mimeTypeForFile(item.m_filePath);
        if (mimeType.isValid() && !mimeType.inherits(QStringLiteral("text/plain"))) {
            appendError(tr("Skip binary file (%1)").arg(item.m_filePath));
            continue;
        }

        searchFile(item.m_filePath, item.m_displayPath);

        if (++nr >= c_batchSize) {
            nr = 0;
            processBatchResults();
        }
    }

    processBatchResults();

    if (m_state == SearchState::Busy) {
        m_state = SearchState::Finished;
    }
}

void FileSearchEngineWorker::appendError(const QString &p_err)
{
    m_errors.append(p_err);
}

void FileSearchEngineWorker::searchFile(const QString &p_filePath, const QString &p_displayPath)
{
    QFile file(p_filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    const bool shouldStartBatchMode = m_token.shouldStartBatchMode();
    if (shouldStartBatchMode) {
        m_token.startBatchMode();
    }

    QSharedPointer<SearchResultItem> resultItem;

    int lineNum = 0;
    QTextStream ins(&file);
    while (!ins.atEnd()) {
        if (isAskedToStop()) {
            m_state = SearchState::Stopped;
            break;
        }

        const auto lineText = ins.readLine();
        bool matched = false;
        if (!shouldStartBatchMode) {
            matched = m_token.matched(lineText);
        } else {
            matched = m_token.matchedInBatchMode(lineText);
        }

        if (matched) {
            if (resultItem) {
                resultItem->addLine(lineNum, lineText);
            } else {
                resultItem = SearchResultItem::createFileItem(p_filePath, p_displayPath, lineNum, lineText);
            }
        }

        if (shouldStartBatchMode && m_token.readyToEndBatchMode()) {
            break;
        }

        ++lineNum;
    }

    if (shouldStartBatchMode) {
        bool allMatched = m_token.readyToEndBatchMode();
        m_token.endBatchMode();

        if (!allMatched) {
            // This file does not meet all the tokens.
            resultItem.reset();
        }
    }

    if (resultItem) {
        m_results.append(resultItem);
    }
}

void FileSearchEngineWorker::processBatchResults()
{
    if (!m_results.isEmpty()) {
        emit resultItemsReady(m_results);
        m_results.clear();
    }
}

FileSearchEngine::FileSearchEngine()
{
}

FileSearchEngine::~FileSearchEngine()
{
    stopInternal();
    clearInternal();
}

void FileSearchEngine::search(const QSharedPointer<SearchOption> &p_option,
                              const SearchToken &p_token,
                              const QVector<SearchSecondPhaseItem> &p_items)
{
    int numThread = QThread::idealThreadCount();
    if (numThread < 1) {
        numThread = 1;
    }

    Q_ASSERT(!p_items.isEmpty());
    if (p_items.size() < numThread) {
        numThread = 1;
    }

    clearWorkers();
    m_workers.reserve(numThread);
    const int totalSize = p_items.size();
    const int step = totalSize / numThread;
    int remain = totalSize % numThread;
    int start = 0;
    for (int i = 0; i < numThread && start < totalSize; ++i) {
        int len = step;
        if (remain) {
            ++len;
            --remain;
        }

        if (start + len > totalSize) {
            len = totalSize - start;
        }

        auto th = QSharedPointer<FileSearchEngineWorker>::create();
        th->setData(p_items.mid(start, len), p_option, p_token);
        connect(th.data(), &FileSearchEngineWorker::finished,
                this, &FileSearchEngine::handleWorkerFinished);
        connect(th.data(), &FileSearchEngineWorker::resultItemsReady,
                this, &FileSearchEngine::resultItemsAdded);

        m_workers.append(th);
        th->start();

        start += len;
    }
}

void FileSearchEngine::stop()
{
    stopInternal();
}

void FileSearchEngine::stopInternal()
{
    for (const auto &th : m_workers) {
        th->stop();
    }
}

void FileSearchEngine::clear()
{
    clearInternal();
}

void FileSearchEngine::clearInternal()
{
    clearWorkers();
}

void FileSearchEngine::clearWorkers()
{
    for (const auto &th : m_workers) {
        th->quit();
        th->wait();
    }

    m_workers.clear();
    m_numOfFinishedWorkers = 0;
}

void FileSearchEngine::handleWorkerFinished()
{
    ++m_numOfFinishedWorkers;
    if (m_numOfFinishedWorkers == m_workers.size()) {
        SearchState state = SearchState::Finished;

        for (const auto &th : m_workers) {
            if (th->m_state == SearchState::Failed) {
                if (state != SearchState::Stopped) {
                    state = SearchState::Failed;
                }
            } else if (th->m_state == SearchState::Stopped) {
                state = SearchState::Stopped;
            }

            for (const auto &err : th->m_errors) {
                emit logRequested(err);
            }

            Q_ASSERT(th->isFinished());
        }

        m_workers.clear();
        m_numOfFinishedWorkers = 0;

        emit finished(state);
    }
}
