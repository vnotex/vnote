#include "notebookconfig.h"

#include <notebook/notebookparameters.h>
#include <notebook/notebook.h>
#include <versioncontroller/iversioncontroller.h>
#include <utils/utils.h>
#include "exception.h"
#include "global.h"

using namespace vnotex;

const QString NotebookConfig::c_version = "version";

const QString NotebookConfig::c_name = "name";

const QString NotebookConfig::c_description = "description";

const QString NotebookConfig::c_imageFolder = "image_folder";

const QString NotebookConfig::c_attachmentFolder = "attachment_folder";

const QString NotebookConfig::c_createdTimeUtc = "created_time";

const QString NotebookConfig::c_versionController = "version_controller";

const QString NotebookConfig::c_configMgr = "config_mgr";

QSharedPointer<NotebookConfig> NotebookConfig::fromNotebookParameters(int p_version,
                                                                      const NotebookParameters &p_paras)
{
    auto config = QSharedPointer<NotebookConfig>::create();

    config->m_version = p_version;
    config->m_name = p_paras.m_name;
    config->m_description = p_paras.m_description;
    config->m_imageFolder = p_paras.m_imageFolder;
    config->m_attachmentFolder = p_paras.m_attachmentFolder;
    config->m_createdTimeUtc = p_paras.m_createdTimeUtc;
    config->m_versionController = p_paras.m_versionController->getName();
    config->m_notebookConfigMgr = p_paras.m_notebookConfigMgr->getName();

    return config;
}

QJsonObject NotebookConfig::toJson() const
{
    QJsonObject jobj;

    jobj[NotebookConfig::c_version] = m_version;
    jobj[NotebookConfig::c_name] = m_name;
    jobj[NotebookConfig::c_description] = m_description;
    jobj[NotebookConfig::c_imageFolder] = m_imageFolder;
    jobj[NotebookConfig::c_attachmentFolder] = m_attachmentFolder;
    jobj[NotebookConfig::c_createdTimeUtc] = Utils::dateTimeStringUniform(m_createdTimeUtc);
    jobj[NotebookConfig::c_versionController] = m_versionController;
    jobj[NotebookConfig::c_configMgr] = m_notebookConfigMgr;

    jobj[QStringLiteral("history")] = saveHistory();

    jobj[QStringLiteral("extra_configs")] = m_extraConfigs;

    return jobj;
}

void NotebookConfig::fromJson(const QJsonObject &p_jobj)
{
    if (!p_jobj.contains(NotebookConfig::c_version)
        || !p_jobj.contains(NotebookConfig::c_name)
        || !p_jobj.contains(NotebookConfig::c_createdTimeUtc)
        || !p_jobj.contains(NotebookConfig::c_versionController)
        || !p_jobj.contains(NotebookConfig::c_configMgr)) {
        Exception::throwOne(Exception::Type::InvalidArgument,
                            QString("failed to read notebook configuration from JSON (%1)").arg(QJsonObjectToString(p_jobj)));
        return;
    }

    m_version = p_jobj[NotebookConfig::c_version].toInt();
    m_name = p_jobj[NotebookConfig::c_name].toString();
    m_description = p_jobj[NotebookConfig::c_description].toString();
    m_imageFolder = p_jobj[NotebookConfig::c_imageFolder].toString();
    m_attachmentFolder = p_jobj[NotebookConfig::c_attachmentFolder].toString();
    m_createdTimeUtc = Utils::dateTimeFromStringUniform(p_jobj[NotebookConfig::c_createdTimeUtc].toString());
    m_versionController = p_jobj[NotebookConfig::c_versionController].toString();
    m_notebookConfigMgr = p_jobj[NotebookConfig::c_configMgr].toString();

    loadHistory(p_jobj);

    m_extraConfigs = p_jobj[QStringLiteral("extra_configs")].toObject();
}

QSharedPointer<NotebookConfig> NotebookConfig::fromNotebook(int p_version,
                                                            const Notebook *p_notebook)
{
    auto config = QSharedPointer<NotebookConfig>::create();

    config->m_version = p_version;
    config->m_name = p_notebook->getName();
    config->m_description = p_notebook->getDescription();
    config->m_imageFolder = p_notebook->getImageFolder();
    config->m_attachmentFolder = p_notebook->getAttachmentFolder();
    config->m_createdTimeUtc = p_notebook->getCreatedTimeUtc();
    config->m_versionController = p_notebook->getVersionController()->getName();
    config->m_notebookConfigMgr = p_notebook->getConfigMgr()->getName();
    config->m_history = p_notebook->getHistory();
    config->m_extraConfigs = p_notebook->getExtraConfigs();

    return config;
}

QJsonArray NotebookConfig::saveHistory() const
{
    QJsonArray arr;
    for (const auto &item : m_history) {
        arr.append(item.toJson());
    }
    return arr;
}

void NotebookConfig::loadHistory(const QJsonObject &p_jobj)
{
    auto arr = p_jobj[QStringLiteral("history")].toArray();
    m_history.resize(arr.size());
    for (int i = 0; i < arr.size(); ++i) {
        m_history[i].fromJson(arr[i].toObject());
    }
}
