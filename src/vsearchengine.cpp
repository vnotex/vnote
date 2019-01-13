#include "vsearchengine.h"

#include <QDebug>
#include <QFile>
#include <QMimeDatabase>

#include "utils/vutils.h"

VSearchEngineWorker::VSearchEngineWorker(QObject *p_parent)
    : QThread(p_parent),
      m_stop(0),
      m_state(VSearchState::Idle)
{
}

void VSearchEngineWorker::setData(const QStringList &p_files,
                                  const VSearchToken &p_token,
                                  const QSharedPointer<VSearchConfig> &p_config)
{
    m_files = p_files;
    m_token = p_token;
    m_config = p_config;
}

void VSearchEngineWorker::stop()
{
    m_stop.store(1);
}

void VSearchEngineWorker::run()
{
    qDebug() << "worker" << QThread::currentThreadId() << m_files.size();

    QMimeDatabase mimeDatabase;
    m_state = VSearchState::Busy;

    m_results.clear();
    int nr = 0;
    for (auto const & fileName : m_files) {
        if (m_stop.load() == 1) {
            m_state = VSearchState::Cancelled;
            qDebug() << "worker" << QThread::currentThreadId() << "is asked to stop";
            break;
        }

        const QMimeType mimeType = mimeDatabase.mimeTypeForFile(fileName);
        if (mimeType.isValid() && !mimeType.inherits(QStringLiteral("text/plain"))) {
            appendError(tr("Skip binary file %1.").arg(fileName));
            continue;
        }

        VSearchResultItem *item = searchFile(fileName);
        if (item) {
            m_results.append(QSharedPointer<VSearchResultItem>(item));
        }

        if (++nr >= BATCH_ITEM_SIZE) {
            nr = 0;
            postAndClearResults();
        }
    }

    postAndClearResults();

    if (m_state == VSearchState::Busy) {
        m_state = VSearchState::Success;
    }
}

VSearchResultItem *VSearchEngineWorker::searchFile(const QString &p_fileName)
{
    QFile file(p_fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        return NULL;
    }

    int lineNum = 1;
    VSearchResultItem *item = NULL;
    QString line;
    QTextStream in(&file);

    bool singleToken = m_token.tokenSize() == 1;
    if (!singleToken) {
        m_token.startBatchMode();
    }

    bool allMatched = false;

    while (!in.atEnd()) {
        if (m_stop.load() == 1) {
            m_state = VSearchState::Cancelled;
            qDebug() << "worker" << QThread::currentThreadId() << "is asked to stop";
            break;
        }

        line = in.readLine();
        bool matched = false;
        if (singleToken) {
            matched = m_token.matched(line);
        } else {
            matched = m_token.matchBatchMode(line);
        }

        if (matched) {
            if (!item) {
                item = new VSearchResultItem(VSearchResultItem::Note,
                                             VSearchResultItem::LineNumber,
                                             VUtils::fileNameFromPath(p_fileName),
                                             p_fileName,
                                             m_config);
            }

            VSearchResultSubItem sitem(lineNum, line);
            item->m_matches.append(sitem);
        }

        if (!singleToken && m_token.readyToEndBatchMode(allMatched)) {
            break;
        }

        ++lineNum;
    }

    if (!singleToken) {
        m_token.readyToEndBatchMode(allMatched);
        m_token.endBatchMode();

        if (!allMatched && item) {
            delete item;
            item = NULL;
        }
    }

    return item;
}

void VSearchEngineWorker::postAndClearResults()
{
    if (!m_results.isEmpty()) {
        emit resultItemsReady(m_results);
        m_results.clear();
    }
}


VSearchEngine::VSearchEngine(QObject *p_parent)
    : ISearchEngine(p_parent),
      m_finishedWorkers(0)
{
}

VSearchEngine::~VSearchEngine()
{
    // stop()
    for (auto const & th : m_workers) {
        th->stop();
    }

    // clear()
    clearAllWorkers();

    m_finishedWorkers = 0;

    m_result.clear();
}

void VSearchEngine::search(const QSharedPointer<VSearchConfig> &p_config,
                           const QSharedPointer<VSearchResult> &p_result)
{
    int numThread = QThread::idealThreadCount();
    if (numThread < 1) {
        numThread = 1;
    }

    const QStringList items = p_result->m_secondPhaseItems;
    Q_ASSERT(!items.isEmpty());
    if (items.size() < numThread) {
        numThread = items.size();
    }

    m_result = p_result;

    clearAllWorkers();
    m_workers.reserve(numThread);
    m_finishedWorkers = 0;
    int totalSize = m_result->m_secondPhaseItems.size();
    int step = totalSize / numThread;
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

        VSearchEngineWorker *th = new VSearchEngineWorker(this);
        th->setData(m_result->m_secondPhaseItems.mid(start, len),
                    p_config->m_contentToken,
                    p_config);
        connect(th, &VSearchEngineWorker::finished,
                this, &VSearchEngine::handleWorkerFinished);
        connect(th, &VSearchEngineWorker::resultItemsReady,
                this, [this](const QList<QSharedPointer<VSearchResultItem> > &p_items) {
                    emit resultItemsAdded(p_items);
                });

        m_workers.append(th);
        th->start();

        start += len;
    }

    qDebug() << "schedule tasks to threads" << m_workers.size() << totalSize << step;
}

void VSearchEngine::stop()
{
    qDebug() << "VSearchEngine asked to stop";
    for (auto const & th : m_workers) {
        th->stop();
    }
}

void VSearchEngine::handleWorkerFinished()
{
    ++m_finishedWorkers;

    qDebug() << m_finishedWorkers << "workers finished";
    if (m_finishedWorkers == m_workers.size()) {
        VSearchState state = VSearchState::Success;

        for (auto const & th : m_workers) {
            if (th->m_state == VSearchState::Fail) {
                if (state != VSearchState::Cancelled) {
                    state = VSearchState::Fail;
                }
            } else if (th->m_state == VSearchState::Cancelled) {
                state = VSearchState::Cancelled;
            }

            if (!th->m_error.isEmpty()) {
                m_result->logError(th->m_error);
            }

            Q_ASSERT(th->isFinished());
            th->deleteLater();
        }

        m_workers.clear();
        m_finishedWorkers = 0;

        m_result->m_state = state;
        qDebug() << "SearchEngine finished" << (int)state;
        emit finished(m_result);
    }
}

void VSearchEngine::clear()
{
    clearAllWorkers();

    m_finishedWorkers = 0;

    m_result.clear();
}

void VSearchEngine::clearAllWorkers()
{
    for (auto const & th : m_workers) {
        th->quit();
        th->wait();

        delete th;
    }

    m_workers.clear();
}
