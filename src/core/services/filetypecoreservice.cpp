#include "filetypecoreservice.h"

#include <QDebug>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <vxcore/vxcore.h>

#include <utils/fileutils2.h>

using namespace vnotex;

FileTypeCoreService::FileTypeCoreService(VxCoreContextHandle p_context, const QString &p_locale,
                                         QObject *p_parent)
    : QObject(p_parent), m_context(p_context), m_locale(p_locale) {}

FileType FileTypeCoreService::parseFileType(const QJsonObject &p_obj) const {
  FileType type;

  type.m_typeName = p_obj["name"].toString();
  const bool isImage = type.m_typeName.compare(QLatin1String("Image"), Qt::CaseInsensitive) == 0;
  type.m_isNewable = !isImage && p_obj["isNewable"].toBool();
  type.m_isSearchable = !isImage && p_obj["isSearchable"].toBool(false);

  // Get default displayName from vxcore
  QString defaultDisplayName = p_obj["displayName"].toString();
  type.m_defaultDisplayName = defaultDisplayName;
  type.m_displayName =
      isImage && defaultDisplayName == QLatin1String("Image") ? tr("Image") : defaultDisplayName;

  // Check for locale-specific displayName in metadata (e.g., displayName_zh_CN)
  if (!m_locale.isEmpty() && p_obj.contains("metadata") && p_obj["metadata"].isObject()) {
    QJsonObject metadata = p_obj["metadata"].toObject();
    QString localeKey = QStringLiteral("displayName_") + m_locale;
    if (metadata.contains(localeKey) && metadata[localeKey].isString()) {
      type.m_displayName = metadata[localeKey].toString();
    }
  }

  // Get suffixes from vxcore
  QJsonArray suffixesArray = p_obj["suffixes"].toArray();
  for (const auto &suffix : suffixesArray) {
    type.m_suffixes << suffix.toString();
  }

  // Preserve metadata for round-trip (save back to vxcore without loss).
  if (p_obj.contains("metadata") && p_obj["metadata"].isObject()) {
    type.m_metadata = p_obj["metadata"].toObject();
  }

  return type;
}

FileType FileTypeCoreService::parseFileTypeFromJson(const char *p_json) const {
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

FileType FileTypeCoreService::defaultImageFileType(const QVector<FileType> &p_types) const {
  QSet<QString> assignedSuffixes;
  for (const auto &type : p_types) {
    for (const auto &suffix : type.m_suffixes) {
      assignedSuffixes.insert(suffix.toLower());
    }
  }

  FileType image;
  image.m_typeName = QStringLiteral("Image");
  image.m_defaultDisplayName = QStringLiteral("Image");
  image.m_displayName = tr("Image");
  image.m_isNewable = false;
  image.m_isSearchable = false;
  const auto formats = QImageReader::supportedImageFormats();
  for (const auto &format : formats) {
    const auto suffix = QString::fromLatin1(format).trimmed().toLower();
    if (!suffix.isEmpty() && !assignedSuffixes.contains(suffix)) {
      image.m_suffixes.append(suffix);
    }
  }
  image.m_suffixes.removeDuplicates();
  image.m_suffixes.sort();
  return image;
}

QVector<FileType> FileTypeCoreService::parseFileTypesFromJson(const char *p_json) const {
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
  bool hasImage = false;
  for (const auto &typeVal : typesArray) {
    if (typeVal.isObject()) {
      fileTypes.push_back(parseFileType(typeVal.toObject()));
      hasImage = hasImage || fileTypes.constLast().m_typeName.compare(QLatin1String("Image"),
                                                                      Qt::CaseInsensitive) == 0;
    }
  }
  if (!hasImage) {
    fileTypes.push_back(defaultImageFileType(fileTypes));
  }

  return fileTypes;
}

FileType FileTypeCoreService::getFileType(const QString &p_filePath) const {
  Q_ASSERT(!p_filePath.isEmpty());

  QFileInfo fi(p_filePath);
  auto suffix = fi.suffix().toLower();

  // Use vxcore API to get file type by suffix
  FileType type = getFileTypeBySuffix(suffix);

  // Explicit suffix mappings win; infer a viewer only for otherwise unknown files.
  if (type.m_typeName == "Others") {
    if (FileUtils2::isImage(p_filePath)) {
      // Keep saved Image metadata/suffixes authoritative even for MIME inference.
      const auto image = getFileTypeByName(QStringLiteral("Image"));
      return image.m_typeName.isEmpty() ? type : image;
    }
    if (FileUtils2::isText(p_filePath)) {
      return getFileType("Text");
    }
  }

  return type;
}

FileType FileTypeCoreService::getFileTypeBySuffix(const QString &p_suffix) const {
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
  // Reject unrelated suffixes before querying the full registry for an absent Image default.
  // A saved Image (including empty/custom suffixes) is never reseeded by this lookup.
  if (type.m_typeName == QLatin1String("Others") &&
      !type.m_suffixes.contains(p_suffix, Qt::CaseInsensitive) &&
      QImageReader::supportedImageFormats().contains(p_suffix.toLatin1().toLower())) {
    const auto image = getFileTypeByName(QStringLiteral("Image"));
    if (image.m_suffixes.contains(p_suffix, Qt::CaseInsensitive)) {
      return image;
    }
  }
  return type;
}

QVector<FileType> FileTypeCoreService::getAllFileTypes() const {
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

FileType FileTypeCoreService::getFileTypeByName(const QString &p_typeName) const {
  char *json_str = nullptr;
  VxCoreError err =
      vxcore_filetype_get_by_name(m_context, p_typeName.toUtf8().constData(), &json_str);

  if (err == VXCORE_ERR_NOT_FOUND &&
      p_typeName.compare(QLatin1String("Image"), Qt::CaseInsensitive) == 0) {
    const auto types = getAllFileTypes();
    for (const auto &type : types) {
      if (type.m_typeName.compare(QLatin1String("Image"), Qt::CaseInsensitive) == 0) {
        return type;
      }
    }
    return FileType();
  }

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

bool FileTypeCoreService::setAllFileTypes(const QVector<FileType> &p_types) {
  QJsonArray typesArray;
  for (const auto &ft : p_types) {
    QJsonObject obj;
    obj["name"] = ft.m_typeName;
    const bool isImage = ft.m_typeName.compare(QLatin1String("Image"), Qt::CaseInsensitive) == 0;
    obj["isNewable"] = !isImage && ft.m_isNewable;
    obj["isSearchable"] = !isImage && ft.m_isSearchable;
    obj["displayName"] =
        ft.m_defaultDisplayName.isEmpty() ? ft.m_displayName : ft.m_defaultDisplayName;

    QJsonArray suffixesArray;
    for (const auto &s : ft.m_suffixes) {
      suffixesArray.append(s);
    }
    obj["suffixes"] = suffixesArray;

    if (!ft.m_metadata.isEmpty()) {
      obj["metadata"] = ft.m_metadata;
    }

    typesArray.append(obj);
  }

  QJsonDocument doc(typesArray);
  QByteArray jsonBytes = doc.toJson(QJsonDocument::Compact);

  VxCoreError err = vxcore_filetype_set(m_context, jsonBytes.constData());
  if (err != VXCORE_OK) {
    qWarning() << "vxcore_filetype_set failed:" << QString::fromUtf8(vxcore_error_message(err));
    return false;
  }

  return true;
}
