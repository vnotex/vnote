#include "bundlenotebook.h"

#include <QDebug>

#include <notebookconfigmgr/bundlenotebookconfigmgr.h>
#include <notebookconfigmgr/notebookconfig.h>
#include <utils/fileutils.h>
#include <core/historymgr.h>
#include <notebookbackend/inotebookbackend.h>

using namespace vnotex;

BundleNotebook::BundleNotebook(const NotebookParameters &p_paras,
                               const QSharedPointer<NotebookConfig> &p_notebookConfig,
                               QObject *p_parent)
    : Notebook(p_paras, p_parent)
{
    m_nextNodeId = p_notebookConfig->m_nextNodeId;
    m_history = p_notebookConfig->m_history;
}

BundleNotebookConfigMgr *BundleNotebook::getBundleNotebookConfigMgr() const
{
    return dynamic_cast<BundleNotebookConfigMgr *>(getConfigMgr().data());
}

ID BundleNotebook::getNextNodeId() const
{
    return m_nextNodeId;
}

ID BundleNotebook::getAndUpdateNextNodeId()
{
    auto id = m_nextNodeId++;
    getBundleNotebookConfigMgr()->writeNotebookConfig();
    return id;
}

void BundleNotebook::updateNotebookConfig()
{
    getBundleNotebookConfigMgr()->writeNotebookConfig();
}

void BundleNotebook::removeNotebookConfig()
{
    getBundleNotebookConfigMgr()->removeNotebookConfig();
}

void BundleNotebook::remove()
{
    // Remove all nodes.
    removeNode(getRootNode());

    // Remove notebook config.
    removeNotebookConfig();

    // Remove notebook root folder if it is empty.
    if (!FileUtils::removeDirIfEmpty(getRootFolderAbsolutePath())) {
        qInfo() << QString("root folder of notebook (%1) is not empty and needs manual clean up")
                          .arg(getRootFolderAbsolutePath());
    }
}

const QVector<HistoryItem> &BundleNotebook::getHistory() const
{
    return m_history;
}

void BundleNotebook::addHistory(const HistoryItem &p_item)
{
    HistoryItem item(p_item);
    item.m_path = getBackend()->getRelativePath(item.m_path);
    HistoryMgr::insertHistoryItem(m_history, item);

    updateNotebookConfig();
}

void BundleNotebook::clearHistory()
{
    m_history.clear();

    updateNotebookConfig();
}
