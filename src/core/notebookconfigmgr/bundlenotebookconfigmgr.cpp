#include "bundlenotebookconfigmgr.h"

#include <QJsonDocument>

#include <notebookbackend/inotebookbackend.h>
#include <notebook/notebookparameters.h>
#include <notebook/bundlenotebook.h>
#include "notebookconfig.h"
#include <utils/pathutils.h>

using namespace vnotex;

const QString BundleNotebookConfigMgr::c_configFolderName = "vx_notebook";

const QString BundleNotebookConfigMgr::c_configName = "vx_notebook.json";

BundleNotebookConfigMgr::BundleNotebookConfigMgr(const QSharedPointer<INotebookBackend> &p_backend,
                                                 QObject *p_parent)
    : INotebookConfigMgr(p_backend, p_parent)
{
}

void BundleNotebookConfigMgr::createEmptySkeleton(const NotebookParameters &p_paras)
{
    getBackend()->makePath(BundleNotebookConfigMgr::c_configFolderName);

    auto config = NotebookConfig::fromNotebookParameters(getCodeVersion(), p_paras);
    writeNotebookConfig(*config);
}

QSharedPointer<NotebookConfig> BundleNotebookConfigMgr::readNotebookConfig() const
{
    return readNotebookConfig(getBackend());
}

void BundleNotebookConfigMgr::writeNotebookConfig()
{
    auto config = NotebookConfig::fromNotebook(getCodeVersion(), getNotebook());
    writeNotebookConfig(*config);
}

void BundleNotebookConfigMgr::writeNotebookConfig(const NotebookConfig &p_config)
{
    getBackend()->writeFile(getConfigFilePath(), p_config.toJson());
}

void BundleNotebookConfigMgr::removeNotebookConfig()
{
    getBackend()->removeDir(getConfigFolderName());
}

QSharedPointer<NotebookConfig> BundleNotebookConfigMgr::readNotebookConfig(
        const QSharedPointer<INotebookBackend> &p_backend)
{
    auto data = p_backend->readFile(getConfigFilePath());

    auto config = QSharedPointer<NotebookConfig>::create();
    config->fromJson(QJsonDocument::fromJson(data).object());

    return config;
}

const QString &BundleNotebookConfigMgr::getConfigFolderName()
{
    return c_configFolderName;
}

const QString &BundleNotebookConfigMgr::getConfigName()
{
    return c_configName;
}

QString BundleNotebookConfigMgr::getConfigFilePath()
{
    return PathUtils::concatenateFilePath(c_configFolderName, c_configName);
}

BundleNotebook *BundleNotebookConfigMgr::getBundleNotebook() const
{
    return dynamic_cast<BundleNotebook *>(getNotebook());
}

bool BundleNotebookConfigMgr::isBuiltInFile(const Node *p_node, const QString &p_name) const
{
    Q_UNUSED(p_node);
    Q_UNUSED(p_name);
    return false;
}

bool BundleNotebookConfigMgr::isBuiltInFolder(const Node *p_node, const QString &p_name) const
{
    if (p_node->isRoot()) {
        return p_name.toLower() == c_configFolderName;
    }
    return false;
}
