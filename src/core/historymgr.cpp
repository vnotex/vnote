#include "historymgr.h"

#include <QDebug>

#include "configmgr.h"
#include "sessionconfig.h"
#include "coreconfig.h"
#include "vnotex.h"
#include "notebookmgr.h"
#include <notebook/notebook.h>
#include <notebook/historyi.h>
#include <notebookbackend/inotebookbackend.h>
#include "exception.h"

using namespace vnotex;

bool HistoryItemFull::operator<(const HistoryItemFull &p_other) const
{
    if (m_item.m_lastAccessedTimeUtc < p_other.m_item.m_lastAccessedTimeUtc) {
        return true;
    } else if (m_item.m_lastAccessedTimeUtc > p_other.m_item.m_lastAccessedTimeUtc) {
        return false;
    } else {
        return m_item.m_path < p_other.m_item.m_path;
    }
}


HistoryMgr::HistoryMgr()
    : m_perNotebookHistoryEnabled(ConfigMgr::getInst().getCoreConfig().isPerNotebookHistoryEnabled())
{
    connect(&VNoteX::getInst().getNotebookMgr(), &NotebookMgr::notebooksUpdated,
            this, &HistoryMgr::loadHistory);

    loadHistory();
}

static bool historyPtrCmp(const QSharedPointer<HistoryItemFull> &p_a, const QSharedPointer<HistoryItemFull> &p_b)
{
    return *p_a < *p_b;
}

void HistoryMgr::loadHistory()
{
    m_history.clear();

    // Load from session.
    {
        const auto &history = ConfigMgr::getInst().getSessionConfig().getHistory();
        for (const auto &item : history) {
            auto fullItem = QSharedPointer<HistoryItemFull>::create();
            fullItem->m_item = item;
            m_history.push_back(fullItem);
        }
    }

    // Load from notebooks.
    if (m_perNotebookHistoryEnabled) {
        const auto &notebooks = VNoteX::getInst().getNotebookMgr().getNotebooks();
        for (const auto &nb : notebooks) {
            auto historyI = nb->history();
            if (!historyI) {
                continue;
            }
            const auto &history = historyI->getHistory();
            const auto &backend = nb->getBackend();
            for (const auto &item : history) {
                auto fullItem = QSharedPointer<HistoryItemFull>::create();
                fullItem->m_item = item;

                // We saved the absolute path by mistake in previous version.
                try {
                    fullItem->m_item.m_path = backend->getFullPath(item.m_path);
                } catch (Exception &p_e) {
                    qWarning() << "skipped loading history item" << item.m_path << "from notebook" << nb->getName() << p_e.what();
                    continue;
                }

                fullItem->m_notebookName = nb->getName();
                m_history.push_back(fullItem);
            }
        }
    }

    std::sort(m_history.begin(), m_history.end(), historyPtrCmp);

    qDebug() << "loaded" << m_history.size() << "history items";

    emit historyUpdated();
}

const QVector<QSharedPointer<HistoryItemFull>> &HistoryMgr::getHistory() const
{
    return m_history;
}

void HistoryMgr::removeFromHistory(const QString &p_itemPath)
{
    for (int i = m_history.size() - 1; i >= 0; --i) {
        if (m_history[i]->m_item.m_path == p_itemPath) {
            m_history.remove(i);
            break;
        }
    }
}

void HistoryMgr::add(const QString &p_path,
                     int p_lineNumber,
                     ViewWindowMode p_mode,
                     bool p_readOnly,
                     Notebook *p_notebook)
{
    const int maxHistoryCount = ConfigMgr::getInst().getCoreConfig().getHistoryMaxCount();
    if (p_path.isEmpty() || maxHistoryCount == 0) {
        return;
    }

    HistoryItem item(p_path, p_lineNumber, QDateTime::currentDateTimeUtc());

    if (p_notebook && m_perNotebookHistoryEnabled && p_notebook->history()) {
        p_notebook->history()->addHistory(item);
    } else {
        auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
        sessionConfig.addHistory(item);
    }

    // Maintain the combined queue.
    {
        removeFromHistory(item.m_path);

        auto fullItem = QSharedPointer<HistoryItemFull>::create();
        fullItem->m_item = item;
        if (p_notebook) {
            fullItem->m_notebookName = p_notebook->getName();
        }
        m_history.append(fullItem);
    }

    // Update m_lastClosedFiles.
    {
        for (int i = m_lastClosedFiles.size() - 1; i >= 0; --i) {
            if (m_lastClosedFiles[i].m_path == p_path) {
                m_lastClosedFiles.remove(i);
                break;
            }
        }

        m_lastClosedFiles.append(LastClosedFile());
        auto &file = m_lastClosedFiles.back();
        file.m_path = p_path;
        file.m_lineNumber = p_lineNumber;
        file.m_mode = p_mode;
        file.m_readOnly = p_readOnly;

        if (m_lastClosedFiles.size() > maxHistoryCount) {
            m_lastClosedFiles.remove(0, m_lastClosedFiles.size() - maxHistoryCount);
        }
    }

    emit historyUpdated();
}

void HistoryMgr::remove(const QVector<QString> &p_paths, Notebook *p_notebook)
{
    for(const QString &p_itemPath : p_paths) {
        if (p_notebook && m_perNotebookHistoryEnabled && p_notebook->history()) {
            p_notebook->history()->removeHistory(p_itemPath);
        } else {
            auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
            sessionConfig.removeHistory(p_itemPath);
        }

        removeFromHistory(p_itemPath);
    }

    emit historyUpdated();
}

void HistoryMgr::removeHistoryItem(QVector<HistoryItem> &p_history, const QString &p_itemPath)
{
    for (int i = p_history.size() - 1; i >= 0; --i) {
        if (p_history[i].m_path == p_itemPath) {
            p_history.remove(i);
            break;
        }
    }
}

void HistoryMgr::insertHistoryItem(QVector<HistoryItem> &p_history, const HistoryItem &p_item)
{
    removeHistoryItem(p_history, p_item.m_path);
    p_history.append(p_item);

    const int maxHistoryCount = ConfigMgr::getInst().getCoreConfig().getHistoryMaxCount();
    if (p_history.size() > maxHistoryCount) {
        p_history.remove(0, p_history.size() - maxHistoryCount);
    }
}

void HistoryMgr::clear()
{
    ConfigMgr::getInst().getSessionConfig().clearHistory();

    if (m_perNotebookHistoryEnabled) {
        const auto &notebooks = VNoteX::getInst().getNotebookMgr().getNotebooks();
        for (const auto &nb : notebooks) {
            if (auto historyI = nb->history()) {
                historyI->clearHistory();
            }
        }
    }

    loadHistory();
}

HistoryMgr::LastClosedFile HistoryMgr::popLastClosedFile()
{
    if (m_lastClosedFiles.isEmpty()) {
        return LastClosedFile();
    }

    auto file = m_lastClosedFiles.back();
    m_lastClosedFiles.pop_back();
    return file;
}
