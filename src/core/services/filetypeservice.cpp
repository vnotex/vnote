#include "filetypeservice.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <vxcore/vxcore.h>

#include <utils/fileutils.h>

using namespace vnotex;

FileTypeService::FileTypeService(VxCoreContextHandle p_context, const QString &p_locale,
                                 QObject *p_parent)
    : QObject(p_parent), m_context(p_context), m_locale(p_locale) {}

FileType FileTypeService::parseFileType(const QJsonObject &p_obj) const {
  FileType type;

  type.m_typeName = p_obj["name"].toString();
  type.m_isNewable = p_obj["isNewable"].toBool();

  // Get default displayName from vxcore
  QString defaultDisplayName = p_obj["displayName"].toString();

  // Check for locale-specific displayName in metadata (e.g., displayName_zh_CN)
  if (!m_locale.isEmpty() && p_obj.contains("metadata") && p_obj["metadata"].isObject()) {
    QJsonObject metadata = p_obj["metadata"].toObject();
    QString localeKey = QStringLiteral("displayName_") + m_locale;
    if (metadata.contains(localeKey) && metadata[localeKey].isString()) {
      type.m_displayName = metadata[localeKey].toString();
    } else {
      type.m_displayName = defaultDisplayName;
    }
  } else {
    type.m_displayName = defaultDisplayName;
  }

  // Get suffixes from vxcore
  QJsonArray suffixesArray = p_obj["suffixes"].toArray();
  for (const auto &suffix : suffixesArray) {
    type.m_suffixes << suffix.toString();
  }

  return type;
}

FileType FileTypeService::parseFileTypeFromJson(const char *p_json) const {
  if (!p_json) {
    return FileType();
  }

  QJsonDocument doc = QJsonDocument::fromJson(QByteArray(p_json));
  if (!doc.isObject()) {
    qWarning() << "Invalid JSON object from vxcore filetype API";
    return FileType();
  }

  return parseFileType(doc.object());
}

QVector<FileType> FileTypeService::parseFileTypesFromJson(const char *p_json) const {
  QVector<FileType> fileTypes;

  if (!p_json) {
    return fileTypes;
  }

  QJsonDocument doc = QJsonDocument::fromJson(QByteArray(p_json));
  if (!doc.isArray()) {
    qWarning() << "Invalid JSON array from vxcore_filetype_list";
    return fileTypes;
  }

  QJsonArray typesArray = doc.array();
  for (const auto &typeVal : typesArray) {
    if (typeVal.isObject()) {
      fileTypes.push_back(parseFileType(typeVal.toObject()));
    }
  }

  return fileTypes;
}

FileType FileTypeService::getFileType(const QString &p_filePath) const {
  Q_ASSERT(!p_filePath.isEmpty());

  QFileInfo fi(p_filePath);
  auto suffix = fi.suffix().toLower();

  // Use vxcore API to get file type by suffix
  FileType type = getFileTypeBySuffix(suffix);

  // If suffix not found (returns Others), check if it's a text file
  if (type.m_typeName == "Others" && FileUtils::isText(p_filePath)) {
    return getFileType("Text");
  }

  return type;
}

FileType FileTypeService::getFileTypeBySuffix(const QString &p_suffix) const {
  char *json_str = nullptr;
  VxCoreError err =
      vxcore_filetype_get_by_suffix(m_context, p_suffix.toUtf8().constData(), &json_str);

  if (err != VXCORE_OK || !json_str) {
    qWarning() << "vxcore_filetype_get_by_suffix failed:"
               << QString::fromUtf8(vxcore_error_message(err));
    return FileType();
  }

  FileType type = parseFileTypeFromJson(json_str);
  vxcore_string_free(json_str);
  return type;
}

QVector<FileType> FileTypeService::getAllFileTypes() const {
  char *json_str = nullptr;
  VxCoreError err = vxcore_filetype_list(m_context, &json_str);

  if (err != VXCORE_OK || !json_str) {
    qWarning() << "vxcore_filetype_list failed:" << QString::fromUtf8(vxcore_error_message(err));
    return QVector<FileType>();
  }

  QVector<FileType> types = parseFileTypesFromJson(json_str);
  vxcore_string_free(json_str);
  return types;
}

FileType FileTypeService::getFileTypeByName(const QString &p_typeName) const {
  char *json_str = nullptr;
  VxCoreError err =
      vxcore_filetype_get_by_name(m_context, p_typeName.toUtf8().constData(), &json_str);

  if (err != VXCORE_OK || !json_str) {
    // Name not found - return empty FileType (vxcore returns VXCORE_ERR_NOT_FOUND)
    if (err != VXCORE_ERR_NOT_FOUND) {
      qWarning() << "vxcore_filetype_get_by_name failed:"
                 << QString::fromUtf8(vxcore_error_message(err));
    }
    return FileType();
  }

  FileType type = parseFileTypeFromJson(json_str);
  vxcore_string_free(json_str);
  return type;
}
