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
                                  const QRegExp &p_reg,
                                  const QString &p_keyword,
                                  Qt::CaseSensitivity p_cs)
{
    m_files = p_files;
    m_reg = p_reg;
    m_keyword = p_keyword;
    m_caseSensitivity = p_cs;
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
            emit resultItemReady(item);
        }
    }

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
    while (!in.atEnd()) {
        if (m_stop.load() == 1) {
            m_state = VSearchState::Cancelled;
            qDebug() << "worker" << QThread::currentThreadId() << "is asked to stop";
            break;
        }

        bool matched = false;
        line = in.readLine();
        if (m_reg.isEmpty()) {
            if (line.contains(m_keyword, m_caseSensitivity)) {
                matched = true;
            }
        } else if (m_reg.indexIn(line) != -1) {
            matched = true;
        }

        if (matched) {
            if (!item) {
                item = new VSearchResultItem(VSearchResultItem::Note,
                                             VSearchResultItem::LineNumber,
                                             VUtils::fileNameFromPath(p_fileName),
                                             p_fileName);
            }

            VSearchResultSubItem sitem(lineNum, line);
            item->m_matches.append(sitem);
        }

        ++lineNum;
    }

    return item;
}


VSearchEngine::VSearchEngine(QObject *p_parent)
    : ISearchEngine(p_parent),
      m_finishedWorkers(0)
{
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

    QRegExp reg = compileRegExpFromConfig(p_config);
    Qt::CaseSensitivity cs = (p_config->m_option & VSearchConfig::CaseSensitive)
                             ? Qt::CaseSensitive : Qt::CaseInsensitive;

    clearAllWorkers();
    m_workers.reserve(numThread);
    m_finishedWorkers = 0;
    int totalSize = m_result->m_secondPhaseItems.size();
    int step = totalSize / numThread;
    int remain = totalSize % numThread;

    for (int i = 0; i < numThread; ++i) {
        int start = i * step;
        if (start >= totalSize) {
            break;
        }

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
                    reg,
                    p_config->m_keyword,
                    cs);
        connect(th, &VSearchEngineWorker::finished,
                this, &VSearchEngine::handleWorkerFinished);
        connect(th, &VSearchEngineWorker::resultItemReady,
                this, [this](VSearchResultItem *p_item) {
                    emit resultItemAdded(QSharedPointer<VSearchResultItem>(p_item));
                });

        m_workers.append(th);
        th->start();
    }

    qDebug() << "schedule tasks to threads" << m_workers.size() << totalSize << step;
}

QRegExp VSearchEngine::compileRegExpFromConfig(const QSharedPointer<VSearchConfig> &p_config) const
{
    const QString &keyword = p_config->m_keyword;
    Qt::CaseSensitivity cs = (p_config->m_option & VSearchConfig::CaseSensitive)
                             ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (p_config->m_option & VSearchConfig::RegularExpression) {
        return QRegExp(keyword, cs);
    } else if (p_config->m_option & VSearchConfig::WholeWordOnly) {
        QString pattern = QRegExp::escape(keyword);
        pattern = "\\b" + pattern + "\\b";
        return QRegExp(pattern, cs);
    } else {
        return QRegExp();
    }
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
