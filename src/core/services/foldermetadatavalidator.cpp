// FolderMetadataValidator — see the header for the contract.
//
// Extracted VERBATIM from the anonymous namespace of foldersharepackager.cpp so
// the import side can reuse exactly the same strictness the share side has been
// exercised against. The only structural change is that the orphan scan takes
// its directory enumerator as a parameter instead of calling the packager's
// file-local one.

#include "foldermetadatavalidator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QObject>

#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

namespace {

const char *const kFolderConfigFile = "vx.json";

bool isReparsePoint(const QString &p_path) {
#ifdef Q_OS_WIN
  const DWORD attrs = ::GetFileAttributesW(
      reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(p_path).utf16()));
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return false; // Does not exist: nothing to traverse.
  }
  return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  const QFileInfo info(p_path);
  return info.exists() && info.isSymLink();
#endif
}

QString foldForCompare(const QString &p_name, bool p_caseSensitive) {
  return p_caseSensitive ? p_name : p_name.toLower();
}

} // namespace

QString FolderMetadataValidator::folderConfigFileName() { return QLatin1String(kFolderConfigFile); }

bool FolderMetadataValidator::isSafeChildName(const QString &p_name) {
  if (p_name.isEmpty() || p_name == QLatin1String(".") || p_name == QLatin1String("..")) {
    return false;
  }
  if (p_name.contains(QLatin1Char('/')) || p_name.contains(QLatin1Char('\\'))) {
    return false;
  }
  return !p_name.contains(QLatin1Char(':'));
}

bool FolderMetadataValidator::isLinkOrReparsePoint(const QString &p_path) {
  return isReparsePoint(p_path) || QFileInfo(p_path).isSymLink();
}

bool FolderMetadataValidator::isNonEmptyString(const QJsonValue &p_value) {
  return p_value.isString() && !p_value.toString().isEmpty();
}

bool FolderMetadataValidator::isIntegralTimestamp(const QJsonValue &p_value) {
  if (!p_value.isDouble()) {
    return false;
  }
  const double raw = p_value.toDouble();
  if (!std::isfinite(raw)) {
    return false;
  }
  if (raw != std::floor(raw)) {
    return false;
  }
  // Beyond 2^53 a double no longer represents consecutive integers exactly.
  constexpr double kMaxExact = 9007199254740992.0;
  return raw >= -kMaxExact && raw <= kMaxExact;
}

bool FolderMetadataValidator::validateFileRecord(const QJsonValue &p_value, QString *p_outName,
                                                 QString *p_outId, QString *p_error) {
  if (!p_value.isObject()) {
    *p_error = QObject::tr("A file record in vx.json is not an object.");
    return false;
  }
  const QJsonObject record = p_value.toObject();

  if (!isNonEmptyString(record.value(QLatin1String(vxcore::kJsonKeyId)))) {
    *p_error = QObject::tr("A file record is missing a valid \"id\".");
    return false;
  }
  if (!isNonEmptyString(record.value(QLatin1String(vxcore::kJsonKeyName)))) {
    *p_error = QObject::tr("A file record is missing a valid \"name\".");
    return false;
  }
  const QString name = record.value(QLatin1String(vxcore::kJsonKeyName)).toString();

  if (!isIntegralTimestamp(record.value(QLatin1String(vxcore::kJsonKeyCreatedUtc))) ||
      !isIntegralTimestamp(record.value(QLatin1String(vxcore::kJsonKeyModifiedUtc)))) {
    *p_error = QObject::tr("File record \"%1\" has non-numeric timestamps.").arg(name);
    return false;
  }
  if (!record.value(QLatin1String(vxcore::kJsonKeyMetadata)).isObject()) {
    *p_error = QObject::tr("File record \"%1\" has a non-object \"metadata\".").arg(name);
    return false;
  }
  if (!record.value(QLatin1String(vxcore::kJsonKeyTags)).isArray()) {
    *p_error = QObject::tr("File record \"%1\" has a non-array \"tags\".").arg(name);
    return false;
  }
  for (const QJsonValue &tag : record.value(QLatin1String(vxcore::kJsonKeyTags)).toArray()) {
    if (!tag.isString()) {
      *p_error = QObject::tr("File record \"%1\" has a non-string tag entry.").arg(name);
      return false;
    }
  }
  // "attachments" is OPTIONAL: the canonical serializer omits it when empty.
  // When present it must be an array of strings.
  if (record.contains(QLatin1String(vxcore::kJsonKeyAttachments))) {
    const QJsonValue attachments = record.value(QLatin1String(vxcore::kJsonKeyAttachments));
    if (!attachments.isArray()) {
      *p_error = QObject::tr("File record \"%1\" has a non-array \"attachments\".").arg(name);
      return false;
    }
    for (const QJsonValue &item : attachments.toArray()) {
      if (!item.isString()) {
        *p_error = QObject::tr("File record \"%1\" has a non-string attachment entry.").arg(name);
        return false;
      }
    }
  }

  *p_outName = name;
  *p_outId = record.value(QLatin1String(vxcore::kJsonKeyId)).toString();
  return true;
}

bool FolderMetadataValidator::validateFolderMetadata(const QString &p_metadataDir,
                                                     const QString &p_contentDir,
                                                     const QString &p_expectedName,
                                                     bool p_caseSensitive,
                                                     QSet<QString> *p_visitedDirs,
                                                     QSet<QString> *p_seenIds, QString *p_error) {
  const QString configPath = p_metadataDir + QLatin1Char('/') + QLatin1String(kFolderConfigFile);

  QFile file(configPath);
  if (!file.exists()) {
    *p_error = QObject::tr("Missing folder metadata: %1").arg(configPath);
    return false;
  }
  if (!file.open(QIODevice::ReadOnly)) {
    *p_error = QObject::tr("Cannot read folder metadata: %1").arg(configPath);
    return false;
  }
  const QByteArray raw = file.readAll();
  file.close();

  QJsonParseError parseError{};
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    *p_error = QObject::tr("Malformed folder metadata: %1").arg(configPath);
    return false;
  }
  const QJsonObject config = doc.object();

  if (!isNonEmptyString(config.value(QLatin1String(vxcore::kJsonKeyId)))) {
    *p_error = QObject::tr("Folder metadata is missing a valid \"id\": %1").arg(configPath);
    return false;
  }
  if (!isNonEmptyString(config.value(QLatin1String(vxcore::kJsonKeyName)))) {
    *p_error = QObject::tr("Folder metadata is missing a valid \"name\": %1").arg(configPath);
    return false;
  }
  if (config.value(QLatin1String(vxcore::kJsonKeyName)).toString() != p_expectedName) {
    *p_error = QObject::tr("Folder metadata name does not match its directory: %1").arg(configPath);
    return false;
  }
  {
    const QString folderId = config.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    if (p_seenIds->contains(folderId)) {
      *p_error = QObject::tr("Duplicate id \"%1\" in %2").arg(folderId, configPath);
      return false;
    }
    p_seenIds->insert(folderId);
  }
  if (!isIntegralTimestamp(config.value(QLatin1String(vxcore::kJsonKeyCreatedUtc))) ||
      !isIntegralTimestamp(config.value(QLatin1String(vxcore::kJsonKeyModifiedUtc)))) {
    *p_error = QObject::tr("Folder metadata has non-numeric timestamps: %1").arg(configPath);
    return false;
  }
  if (!config.value(QLatin1String(vxcore::kJsonKeyMetadata)).isObject()) {
    *p_error = QObject::tr("Folder metadata has a non-object \"metadata\": %1").arg(configPath);
    return false;
  }
  if (!config.value(QLatin1String(vxcore::kJsonKeyFiles)).isArray()) {
    *p_error = QObject::tr("Folder metadata has a non-array \"files\": %1").arg(configPath);
    return false;
  }
  if (!config.value(QLatin1String(vxcore::kJsonKeyFolders)).isArray()) {
    *p_error = QObject::tr("Folder metadata has a non-array \"folders\": %1").arg(configPath);
    return false;
  }

  p_visitedDirs->insert(QDir::cleanPath(p_metadataDir));

  QSet<QString> seenNames;

  // Files: unique, safe, physically present as regular files.
  for (const QJsonValue &value : config.value(QLatin1String(vxcore::kJsonKeyFiles)).toArray()) {
    QString name;
    QString fileId;
    if (!validateFileRecord(value, &name, &fileId, p_error)) {
      return false;
    }
    if (p_seenIds->contains(fileId)) {
      *p_error = QObject::tr("Duplicate id \"%1\" in %2").arg(fileId, configPath);
      return false;
    }
    p_seenIds->insert(fileId);
    if (!isSafeChildName(name)) {
      *p_error = QObject::tr("Unsafe child name \"%1\" in %2").arg(name, configPath);
      return false;
    }
    const QString folded = foldForCompare(name, p_caseSensitive);
    if (seenNames.contains(folded)) {
      *p_error =
          QObject::tr("Duplicate or colliding child name \"%1\" in %2").arg(name, configPath);
      return false;
    }
    seenNames.insert(folded);

    const QString childContent = p_contentDir + QLatin1Char('/') + name;
    if (isLinkOrReparsePoint(childContent)) {
      *p_error = QObject::tr("Refusing to share: %1 is a symbolic link, junction or reparse point.")
                     .arg(childContent);
      return false;
    }
    const QFileInfo contentInfo(childContent);
    if (!contentInfo.exists() || !contentInfo.isFile()) {
      *p_error =
          QObject::tr("Indexed file \"%1\" is missing from disk under %2").arg(name, p_contentDir);
      return false;
    }
  }

  // Folders: unique, safe, with a matching descendant config AND directory.
  for (const QJsonValue &value : config.value(QLatin1String(vxcore::kJsonKeyFolders)).toArray()) {
    if (!value.isString()) {
      *p_error = QObject::tr("A folder entry in %1 is not a string.").arg(configPath);
      return false;
    }
    const QString name = value.toString();
    if (!isSafeChildName(name)) {
      *p_error = QObject::tr("Unsafe child name \"%1\" in %2").arg(name, configPath);
      return false;
    }
    const QString folded = foldForCompare(name, p_caseSensitive);
    if (seenNames.contains(folded)) {
      *p_error =
          QObject::tr("Duplicate or colliding child name \"%1\" in %2").arg(name, configPath);
      return false;
    }
    seenNames.insert(folded);

    const QString childContent = p_contentDir + QLatin1Char('/') + name;
    if (isLinkOrReparsePoint(childContent)) {
      *p_error = QObject::tr("Refusing to share: %1 is a symbolic link, junction or reparse point.")
                     .arg(childContent);
      return false;
    }
    const QFileInfo childInfo(childContent);
    if (!childInfo.exists() || !childInfo.isDir()) {
      *p_error = QObject::tr("Indexed folder \"%1\" is missing from disk under %2")
                     .arg(name, p_contentDir);
      return false;
    }

    if (!validateFolderMetadata(p_metadataDir + QLatin1Char('/') + name, childContent, name,
                                p_caseSensitive, p_visitedDirs, p_seenIds, p_error)) {
      return false;
    }
  }

  return true;
}

bool FolderMetadataValidator::rejectOrphanMetadata(const QString &p_metadataRoot,
                                                   const QSet<QString> &p_visitedDirs,
                                                   const DirectoryLister &p_lister,
                                                   QString *p_error) {
  QStringList dirs;
  if (!p_lister(p_metadataRoot, &dirs, p_error)) {
    return false;
  }
  for (const QString &rel : dirs) {
    const QString dir = QDir::cleanPath(p_metadataRoot + QLatin1Char('/') + rel);
    const QFileInfo configInfo(dir + QLatin1Char('/') + QLatin1String(kFolderConfigFile));
    if (configInfo.exists() && !p_visitedDirs.contains(dir)) {
      *p_error = QObject::tr("Orphan folder metadata found at %1").arg(dir);
      return false;
    }
  }
  return true;
}

bool FolderMetadataValidator::validateMetadataSubtree(
    const QString &p_metadataRoot, const QString &p_contentRoot, const QString &p_folderName,
    bool p_caseSensitive, const DirectoryLister &p_lister, QString *p_error) {
  QSet<QString> visited;
  QSet<QString> seenIds;
  if (!validateFolderMetadata(p_metadataRoot, p_contentRoot, p_folderName, p_caseSensitive,
                              &visited, &seenIds, p_error)) {
    return false;
  }
  return rejectOrphanMetadata(p_metadataRoot, visited, p_lister, p_error);
}
