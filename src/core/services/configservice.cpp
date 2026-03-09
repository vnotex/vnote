#include "configservice.h"

#include <QJsonDocument>
#include <QJsonParseError>

#include <vxcore/vxcore.h>

using namespace vnotex;
using namespace vnotex::core;

ConfigService::ConfigService(VxCoreContextHandle p_context, QObject *p_parent)
    : QObject(p_parent), m_context(p_context) {}

QString ConfigService::getExecutionFilePath() const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  vxcore_get_execution_file_path(&path);
  return cstrToQString(path);
}

QString ConfigService::getExecutionFolderPath() const {
  if (!checkContext()) {
    return QString();
  }

  char *path = nullptr;
  vxcore_get_execution_folder_path(&path);
  return cstrToQString(path);
}

QString ConfigService::getDataPath(DataLocation p_location) const {
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

QString ConfigService::getConfigPath() const {
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

QString ConfigService::getSessionConfigPath() const {
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

QJsonObject ConfigService::getConfig() const {
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

QJsonObject ConfigService::getSessionConfig() const {
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

QJsonObject ConfigService::getConfigByName(DataLocation p_location,
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

QJsonObject ConfigService::getConfigByNameWithDefaults(DataLocation p_location,
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

Error ConfigService::updateConfigByName(DataLocation p_location, const QString &p_baseName,
                                        const QJsonObject &p_json) {
  if (!checkContext()) {
    return Error::error(ErrorCode::InvalidArgument, "Context is null");
  }

  QString jsonStr = QJsonDocument(p_json).toJson(QJsonDocument::Compact);
  VxCoreError err = vxcore_context_update_config_by_name(
      m_context, static_cast<VxCoreDataLocation>(p_location),
      p_baseName.toUtf8().constData(), jsonStr.toUtf8().constData());
  if (err != VXCORE_OK) {
    return Error::error(ErrorCode::FailToWriteFile,
                        QString("Failed to update config: %1").arg(err));
  }
  return Error::ok();
}

QString ConfigService::cstrToQString(char *p_str) {
  if (!p_str) {
    return QString();
  }
  QString result = QString::fromUtf8(p_str);
  vxcore_string_free(p_str);
  return result;
}

QJsonObject ConfigService::parseJsonObjectFromCStr(char *p_str) {
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

bool ConfigService::checkContext() const { return m_context != nullptr; }
