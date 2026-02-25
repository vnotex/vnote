#include "notebookconfig.h"

#include "exception.h"
#include "global.h"
#include <notebook/bundlenotebook.h>
#include <notebook/notebookparameters.h>
#include <utils/utils.h>
#include <versioncontroller/iversioncontroller.h>

using namespace vnotex;

QSharedPointer<NotebookConfig>
NotebookConfig::fromNotebookParameters(int p_version, const NotebookParameters &p_paras) {
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

QJsonObject NotebookConfig::toJson() const {
  QJsonObject jobj;

  jobj[QStringLiteral("version")] = m_version;
  jobj[QStringLiteral("name")] = m_name;
  jobj[QStringLiteral("description")] = m_description;
  jobj[QStringLiteral("imageFolder")] = m_imageFolder;
  jobj[QStringLiteral("attachmentFolder")] = m_attachmentFolder;
  jobj[QStringLiteral("createdTime")] = Utils::dateTimeStringUniform(m_createdTimeUtc);
  jobj[QStringLiteral("versionController")] = m_versionController;
  jobj[QStringLiteral("configMgr")] = m_notebookConfigMgr;

  jobj[QStringLiteral("history")] = saveHistory();

  jobj[QStringLiteral("tagGraph")] = m_tagGraph;

  jobj[QStringLiteral("extraConfigs")] = m_extraConfigs;

  return jobj;
}

void NotebookConfig::fromJson(const QJsonObject &p_jobj) {
  if (!p_jobj.contains(QStringLiteral("version")) || !p_jobj.contains(QStringLiteral("name")) ||
      !p_jobj.contains(QStringLiteral("createdTime")) ||
      !p_jobj.contains(QStringLiteral("versionController")) ||
      !p_jobj.contains(QStringLiteral("configMgr"))) {
    Exception::throwOne(Exception::Type::InvalidArgument,
                        QStringLiteral("failed to read notebook configuration from JSON (%1)")
                            .arg(QJsonObjectToString(p_jobj)));
    return;
  }

  m_version = p_jobj[QStringLiteral("version")].toInt();
  m_name = p_jobj[QStringLiteral("name")].toString();
  m_description = p_jobj[QStringLiteral("description")].toString();
  m_imageFolder = p_jobj[QStringLiteral("imageFolder")].toString();
  m_attachmentFolder = p_jobj[QStringLiteral("attachmentFolder")].toString();
  m_createdTimeUtc =
      Utils::dateTimeFromStringUniform(p_jobj[QStringLiteral("createdTime")].toString());
  m_versionController = p_jobj[QStringLiteral("versionController")].toString();
  m_notebookConfigMgr = p_jobj[QStringLiteral("configMgr")].toString();

  loadHistory(p_jobj);

  m_tagGraph = p_jobj[QStringLiteral("tagGraph")].toString();

  m_extraConfigs = p_jobj[QStringLiteral("extraConfigs")].toObject();
}

QSharedPointer<NotebookConfig> NotebookConfig::fromNotebook(int p_version,
                                                            const BundleNotebook *p_notebook) {
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
  config->m_tagGraph = p_notebook->getTagGraph();
  config->m_extraConfigs = p_notebook->getExtraConfigs();

  return config;
}

QJsonArray NotebookConfig::saveHistory() const {
  QJsonArray arr;
  for (const auto &item : m_history) {
    arr.append(item.toJson());
  }
  return arr;
}

void NotebookConfig::loadHistory(const QJsonObject &p_jobj) {
  auto arr = p_jobj[QStringLiteral("history")].toArray();
  m_history.resize(arr.size());
  for (int i = 0; i < arr.size(); ++i) {
    m_history[i].fromJson(arr[i].toObject());
  }
}
