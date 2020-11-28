#include "bundlenotebook.h"

#include <QDebug>

#include <notebookconfigmgr/bundlenotebookconfigmgr.h>
#include <notebookconfigmgr/notebookconfig.h>
#include <utils/fileutils.h>

using namespace vnotex;

BundleNotebook::BundleNotebook(const NotebookParameters &p_paras,
                               QObject *p_parent)
    : Notebook(p_paras, p_parent)
{
    auto configMgr = getBundleNotebookConfigMgr();
    auto config = configMgr->readNotebookConfig();
    m_nextNodeId = config->m_nextNodeId;
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
