#include "configcoreservice.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonParseError>

#include <vxcore/vxcore.h>

using namespace vnotex;

ConfigCoreService::ConfigCoreService(VxCoreContextHandle p_context, QObject *p_parent)
    : QObject(p_parent), m_context(p_context) {}

QString ConfigCoreService::getExecutionFilePath() const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  vxcore_get_execution_file_path(&path);
  return cstrToQString(path);
}

QString ConfigCoreService::getExecutionFolderPath() const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  vxcore_get_execution_folder_path(&path);
  return cstrToQString(path);
}

QString ConfigCoreService::getDataPath(DataLocation p_location) const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err = vxcore_context_get_data_path(
      m_context, static_cast<VxCoreDataLocation>(p_location), &path);
  if (err != VXCORE_OK) {
    return QString();
  }
  return cstrToQString(path);
}

QString ConfigCoreService::getConfigPath() const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err = vxcore_context_get_config_path(m_context, &path);
  if (err != VXCORE_OK) {
    return QString();
  }
  return cstrToQString(path);
}

QString ConfigCoreService::getSessionConfigPath() const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  VxCoreError err = vxcore_context_get_session_config_path(m_context, &path);
  if (err != VXCORE_OK) {
    return QString();
  }
  return cstrToQString(path);
}

QJsonObject ConfigCoreService::getConfig() const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_context_get_config(m_context, &json);
  if (err != VXCORE_OK) {
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

QJsonObject ConfigCoreService::getSessionConfig() const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_context_get_session_config(m_context, &json);
  if (err != VXCORE_OK) {
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

QJsonObject ConfigCoreService::getConfigByName(DataLocation p_location,
                                           const QString &p_baseName) const {
  if (!checkContext()) {
    return QJsonObject();
  }

  char *json = nullptr;
  VxCoreError err = vxcore_context_get_config_by_name(
      m_context, static_cast<VxCoreDataLocation>(p_location),
      p_baseName.toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

QJsonObject ConfigCoreService::getConfigByNameWithDefaults(DataLocation p_location,
                                                       const QString &p_baseName,
                                                       const QJsonObject &p_defaults) const {
  if (!checkContext()) {
    return p_defaults;
  }

  char *json = nullptr;
  VxCoreError err = vxcore_context_get_config_by_name_with_defaults(
      m_context, static_cast<VxCoreDataLocation>(p_location),
      p_baseName.toUtf8().constData(),
      QJsonDocument(p_defaults).toJson(QJsonDocument::Compact).constData(), &json);
  if (err != VXCORE_OK) {
    return QJsonObject();
  }
  return parseJsonObjectFromCStr(json);
}

Error ConfigCoreService::updateConfigByName(DataLocation p_location, const QString &p_baseName,
                                        const QJsonObject &p_json) {
  if (!checkContext()) {
    return Error::error(ErrorCode::InvalidArgument, "Context is null");
  }

  QString jsonStr = QJsonDocument(p_json).toJson(QJsonDocument::Indented);
  VxCoreError err = vxcore_context_update_config_by_name(
      m_context, static_cast<VxCoreDataLocation>(p_location),
      p_baseName.toUtf8().constData(), jsonStr.toUtf8().constData());
  if (err != VXCORE_OK) {
    return Error::error(ErrorCode::FailToWriteFile,
                        QString("Failed to update config: %1").arg(err));
  }
  return Error::ok();
}

QString ConfigCoreService::cstrToQString(char *p_str) {
  if (!p_str) {
    return QString();
  }
  QString result = QString::fromUtf8(p_str);
  vxcore_string_free(p_str);
  return result;
}

QJsonObject ConfigCoreService::parseJsonObjectFromCStr(char *p_str) {
  if (!p_str) {
    return QJsonObject();
  }
  QJsonParseError parseError;
  QJsonDocument doc = QJsonDocument::fromJson(QByteArray(p_str), &parseError);
  vxcore_string_free(p_str);
  if (parseError.error != QJsonParseError::NoError) {
    return QJsonObject();
  }
  return doc.object();
}

bool ConfigCoreService::checkContext() const { return m_context != nullptr; }

bool ConfigCoreService::isRecoverLastSessionEnabled() const {
  QJsonObject config = getConfig();
  return config.value(QStringLiteral("recoverLastSession")).toBool(true);
}

bool ConfigCoreService::setRecoverLastSessionEnabled(bool p_enabled) {
  if (!checkContext()) {
    return false;
  }

  QJsonObject update;
  update[QStringLiteral("recoverLastSession")] = p_enabled;
  QString jsonStr = QJsonDocument(update).toJson(QJsonDocument::Compact);

  VxCoreError err = vxcore_context_update_config(m_context, jsonStr.toUtf8().constData());
  return err == VXCORE_OK;
}

bool ConfigCoreService::prepareShutdown() {
  if (!checkContext()) {
    qWarning() << "ConfigCoreService::prepareShutdown: context is null";
    return false;
  }
  qInfo() << "ConfigCoreService::prepareShutdown: persisting session state...";
  VxCoreError err = vxcore_prepare_shutdown(m_context);
  if (err != VXCORE_OK) {
    qWarning() << "ConfigCoreService::prepareShutdown: vxcore_prepare_shutdown failed:"
               << vxcore_error_message(err);
    return false;
  }
  qInfo() << "ConfigCoreService::prepareShutdown: done";
  return true;
}

bool ConfigCoreService::cancelShutdown() {
  if (!checkContext()) {
    qWarning() << "ConfigCoreService::cancelShutdown: context is null";
    return false;
  }
  VxCoreError err = vxcore_cancel_shutdown(m_context);
  if (err != VXCORE_OK) {
    qWarning() << "ConfigCoreService::cancelShutdown: vxcore_cancel_shutdown failed:"
               << vxcore_error_message(err);
    return false;
  }
  qInfo() << "ConfigCoreService::cancelShutdown: shutdown cancelled, normal operation resumed";
  return true;
}
