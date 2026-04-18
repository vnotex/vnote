#include "vnote3migrationservice.h"

#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <core/services/notebookcoreservice.h>

using namespace vnotex;

// Test-only failpoint: when true, convertNotebook() fails immediately after notebook creation.
static bool s_failAfterCreate = false;

namespace vnotex {

void vnote3MigrationService_setFailAfterCreate(bool p_fail) { s_failAfterCreate = p_fail; }

} // namespace vnotex

namespace {

const QString c_configFolderName = QStringLiteral("vx_notebook");
const QString c_configFileName = QStringLiteral("vx_notebook.json");
const int c_requiredVersion = 3;
const QString c_requiredConfigMgr = QStringLiteral("vx.vnotex");

LegacyTimestamp parseLegacyTimestamp(const QJsonValue &p_val) {
  LegacyTimestamp ts;
  if (p_val.isUndefined() || p_val.isNull()) {
    // Field absent: value=0, parseSucceeded=true (missing is not an error).
    return ts;
  }
  const QString str = p_val.toString();
  if (str.isEmpty()) {
    // Empty string: treat same as absent.
    return ts;
  }
  const QDateTime dt = QDateTime::fromString(str, Qt::ISODate);
  if (!dt.isValid()) {
    // Present but malformed.
    ts.value = 0;
    ts.parseSucceeded = false;
    return ts;
  }
  ts.value = dt.toMSecsSinceEpoch();
  ts.parseSucceeded = true;
  return ts;
}

// Recursively copy a directory tree using raw filesystem operations.
// Does NOT use importFile/createFolderPath — purely filesystem copy.
void copyDirectoryRecursively(const QString &p_srcDir, const QString &p_destDir) {
  QDir srcDir(p_srcDir);
  if (!srcDir.exists()) {
    return;
  }
  QDir().mkpath(p_destDir);
  const auto entries =
      srcDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QFileInfo &fi : entries) {
    const QString destPath = p_destDir + QStringLiteral("/") + fi.fileName();
    if (fi.isDir()) {
      copyDirectoryRecursively(fi.absoluteFilePath(), destPath);
    } else {
      QFile::copy(fi.absoluteFilePath(), destPath);
    }
  }
}

} // namespace

VNote3MigrationService::VNote3MigrationService(NotebookCoreService *p_notebookService,
                                               QObject *p_parent)
    : QObject(p_parent), m_notebookService(p_notebookService) {}

VNote3SourceInspectionResult
VNote3MigrationService::inspectSourceNotebook(const QString &p_sourcePath) const {
  VNote3SourceInspectionResult result;
  result.valid = false;

  // Build path to config file: {sourcePath}/vx_notebook/vx_notebook.json
  const QString configFilePath = p_sourcePath + QStringLiteral("/") + c_configFolderName +
                                 QStringLiteral("/") + c_configFileName;

  // 1. Check config file exists.
  QFile configFile(configFilePath);
  if (!configFile.exists()) {
    result.errorMessage =
        QStringLiteral("VNote3 notebook config not found: %1").arg(configFilePath);
    return result;
  }

  // 2. Read and parse JSON.
  if (!configFile.open(QIODevice::ReadOnly)) {
    result.errorMessage = QStringLiteral("Failed to open config file: %1").arg(configFilePath);
    return result;
  }

  const QByteArray data = configFile.readAll();
  configFile.close();

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    result.errorMessage =
        QStringLiteral("Malformed JSON in config file: %1").arg(parseError.errorString());
    return result;
  }

  const QJsonObject jobj = doc.object();

  // 3. Validate version == 3.
  if (!jobj.contains(QStringLiteral("version")) ||
      jobj[QStringLiteral("version")].toInt(-1) != c_requiredVersion) {
    result.errorMessage =
        QStringLiteral("Unsupported notebook version (expected %1)").arg(c_requiredVersion);
    return result;
  }

  // 4. Validate config_mgr == "vx.vnotex".
  const QString configMgr = jobj[QStringLiteral("config_mgr")].toString();
  if (configMgr != c_requiredConfigMgr) {
    result.errorMessage = QStringLiteral("Unsupported config_mgr: '%1' (expected '%2')")
                              .arg(configMgr, c_requiredConfigMgr);
    return result;
  }

  // 5. Reject absolute image_folder or attachment_folder.
  const QString imageFolder = jobj[QStringLiteral("image_folder")].toString();
  if (!imageFolder.isEmpty() && QDir::isAbsolutePath(imageFolder)) {
    result.errorMessage =
        QStringLiteral("image_folder must be a relative path, got: '%1'").arg(imageFolder);
    return result;
  }

  const QString attachmentFolder = jobj[QStringLiteral("attachment_folder")].toString();
  if (!attachmentFolder.isEmpty() && QDir::isAbsolutePath(attachmentFolder)) {
    result.errorMessage = QStringLiteral("attachment_folder must be a relative path, got: '%1'")
                              .arg(attachmentFolder);
    return result;
  }

  // All checks passed — populate result.
  result.valid = true;
  result.errorMessage.clear();
  result.notebookName = jobj[QStringLiteral("name")].toString();
  result.notebookDescription = jobj[QStringLiteral("description")].toString();
  result.imageFolder = imageFolder;
  result.attachmentFolder = attachmentFolder;

  // Fixed ordered warnings list.
  result.warnings.append(
      QStringLiteral("History, tag graph, and extra configs will not be migrated"));
  result.warnings.append(
      QStringLiteral("Per-node metadata (tags, visual settings) will be migrated to new format"));
  result.warnings.append(QStringLiteral("Recycle bin contents will not be migrated"));

  return result;
}

VNote3ConversionResult VNote3MigrationService::convertNotebook(const QString &p_sourcePath,
                                                               const QString &p_destPath) {
  VNote3ConversionResult result;
  result.success = false;

  // 1. Re-run inspection and fail fast if invalid.
  auto inspection = inspectSourceNotebook(p_sourcePath);
  if (!inspection.valid) {
    result.errorMessage =
        QStringLiteral("Source notebook inspection failed: %1").arg(inspection.errorMessage);
    return result;
  }

  // Pass 1: Pre-scan — collect all unique tags.
  const QStringList allTags = collectAllTags(p_sourcePath);

  // 2. Build destination config JSON with ONLY name and description.
  QJsonObject destConfig;
  destConfig[QStringLiteral("name")] = inspection.notebookName;
  destConfig[QStringLiteral("description")] = inspection.notebookDescription;
  const QString configJson =
      QString::fromUtf8(QJsonDocument(destConfig).toJson(QJsonDocument::Compact));

  // 3. Create the destination notebook.
  const QString notebookId =
      m_notebookService->createNotebook(p_destPath, configJson, NotebookType::Bundled);
  if (notebookId.isEmpty()) {
    result.errorMessage =
        QStringLiteral("Failed to create destination notebook at: %1").arg(p_destPath);
    return result;
  }

  // Test-only failpoint: simulate failure immediately after notebook creation.
  if (s_failAfterCreate) {
    result.errorMessage = QStringLiteral("Failpoint: forced failure after notebook creation");
    m_notebookService->closeNotebook(notebookId);
    QDir(p_destPath).removeRecursively();
    return result;
  }

  // Create all tags in the destination notebook.
  for (const QString &tag : allTags) {
    if (!m_notebookService->createTag(notebookId, tag)) {
      result.warnings.append(
          QStringLiteral("Failed to create tag '%1', skipping").arg(tag));
    }
  }

  // Pass 2: Main import — recursive, vx.json-driven.
  const QDir sourceRoot(p_sourcePath);
  importFolder(notebookId, sourceRoot, QString(), p_destPath, inspection, result.warnings);

  // 7. Close the notebook and return success.
  if (!m_notebookService->closeNotebook(notebookId)) {
    result.errorMessage = QStringLiteral("Failed to close destination notebook");
    QDir(p_destPath).removeRecursively();
    return result;
  }

  result.success = true;
  result.destinationPath = p_destPath;
  result.notebookName = inspection.notebookName;
  return result;
}

void VNote3MigrationService::importFolder(const QString &p_notebookId,
                                          const QDir &p_sourceRoot,
                                          const QString &p_relFolderPath,
                                          const QString &p_destPath,
                                          const VNote3SourceInspectionResult &p_inspection,
                                          QStringList &p_warnings) {
  // Build absolute source path for this folder.
  const QString absFolderPath = p_relFolderPath.isEmpty()
      ? p_sourceRoot.absolutePath()
      : p_sourceRoot.absolutePath() + QStringLiteral("/") + p_relFolderPath;

  // 1. Parse the folder's vx.json.
  const QString vxJsonPath = absFolderPath + QStringLiteral("/vx.json");
  const LegacyVxJson vxJson = parseLegacyVxJson(vxJsonPath);

  // 2. Build set of names tracked by vx.json for orphan detection.
  QSet<QString> vxJsonNames;
  for (const LegacyFileEntry &fe : vxJson.files) {
    if (!fe.name.isEmpty()) {
      vxJsonNames.insert(fe.name);
    }
  }
  for (const LegacyFolderEntry &fe : vxJson.folders) {
    if (!fe.name.isEmpty()) {
      vxJsonNames.insert(fe.name);
    }
  }

  // --- Process file entries from vx.json ---
  for (const LegacyFileEntry &entry : vxJson.files) {
    if (entry.name.isEmpty()) {
      continue;
    }

    const QString filePath = p_relFolderPath.isEmpty()
        ? entry.name
        : p_relFolderPath + QStringLiteral("/") + entry.name;
    const QString absSourceFile = absFolderPath + QStringLiteral("/") + entry.name;

    // 3. Import file.
    try {
      const QString fileId =
          m_notebookService->importFile(p_notebookId, p_relFolderPath, absSourceFile);
      if (fileId.isEmpty()) {
        p_warnings.append(
            QStringLiteral("File '%1': import failed, skipping").arg(filePath));
        continue;
      }
    } catch (...) {
      p_warnings.append(
          QStringLiteral("File '%1': import threw exception, skipping").arg(filePath));
      continue;
    }

    // 4. Apply timestamps.
    {
      qint64 createdMs = entry.createdTime.value;
      qint64 modifiedMs = entry.modifiedTime.value;
      if (!entry.createdTime.parseSucceeded) {
        p_warnings.append(
            QStringLiteral("File '%1': malformed created_time, skipping timestamp")
                .arg(filePath));
        createdMs = 0;
      }
      if (!entry.modifiedTime.parseSucceeded) {
        p_warnings.append(
            QStringLiteral("File '%1': malformed modified_time, skipping timestamp")
                .arg(filePath));
        modifiedMs = 0;
      }
      if (createdMs > 0 || modifiedMs > 0) {
        try {
          m_notebookService->updateNodeTimestamps(
              p_notebookId, filePath, createdMs, modifiedMs);
        } catch (...) {
          p_warnings.append(
              QStringLiteral("File '%1': failed to set timestamps").arg(filePath));
        }
      }
    }

    // 5. Apply tags.
    if (!entry.tags.isEmpty()) {
      try {
        m_notebookService->updateFileTags(p_notebookId, filePath, entry.tags);
      } catch (...) {
        p_warnings.append(
            QStringLiteral("File '%1': failed to set tags").arg(filePath));
      }
    }

    // 6. Apply visual metadata.
    if (!entry.nameColor.isEmpty() || !entry.backgroundColor.isEmpty()) {
      QJsonObject metaObj;
      if (!entry.nameColor.isEmpty()) {
        metaObj[QStringLiteral("textColor")] = entry.nameColor;
      }
      if (!entry.backgroundColor.isEmpty()) {
        metaObj[QStringLiteral("backgroundColor")] = entry.backgroundColor;
      }
      const QString metadataJson =
          QString::fromUtf8(QJsonDocument(metaObj).toJson(QJsonDocument::Compact));
      try {
        m_notebookService->updateFileMetadata(p_notebookId, filePath, metadataJson);
      } catch (...) {
        p_warnings.append(
            QStringLiteral("File '%1': failed to set visual metadata").arg(filePath));
      }
    }

    // 7. Migrate attachments.
    if (!entry.attachmentFolder.isEmpty()) {
      const QString srcAttachDir = absFolderPath + QStringLiteral("/") +
          p_inspection.attachmentFolder + QStringLiteral("/") + entry.attachmentFolder;
      if (QDir(srcAttachDir).exists()) {
        try {
          const QString destAttachDir =
              m_notebookService->getAttachmentsFolder(p_notebookId, filePath);
          if (!destAttachDir.isEmpty()) {
            QDir().mkpath(destAttachDir);
            // Copy root-level files and collect names; recurse into subdirs.
            QStringList rootFileNames;
            const QDir srcAttDir(srcAttachDir);
            const auto attachEntries =
                srcAttDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &afi : attachEntries) {
              if (afi.isFile()) {
                QFile::copy(afi.absoluteFilePath(),
                            destAttachDir + QStringLiteral("/") + afi.fileName());
                rootFileNames.append(afi.fileName());
              } else if (afi.isDir()) {
                copyDirectoryRecursively(
                    afi.absoluteFilePath(),
                    destAttachDir + QStringLiteral("/") + afi.fileName());
              }
            }
            if (!rootFileNames.isEmpty()) {
              m_notebookService->updateFileAttachments(
                  p_notebookId, filePath, rootFileNames);
            }
          }
        } catch (...) {
          p_warnings.append(
              QStringLiteral("File '%1': failed to migrate attachments").arg(filePath));
        }
      } else {
        p_warnings.append(
            QStringLiteral("File '%1': attachment source dir not found, skipping")
                .arg(filePath));
      }
    }
  }

  // --- Process folder entries from vx.json ---
  for (const LegacyFolderEntry &folderEntry : vxJson.folders) {
    if (folderEntry.name.isEmpty()) {
      continue;
    }

    const QString subfolderRelPath = p_relFolderPath.isEmpty()
        ? folderEntry.name
        : p_relFolderPath + QStringLiteral("/") + folderEntry.name;

    // 8-9. Create folder.
    try {
      const QString folderId =
          m_notebookService->createFolderPath(p_notebookId, subfolderRelPath);
      if (folderId.isEmpty()) {
        p_warnings.append(
            QStringLiteral("Folder '%1': creation failed, skipping").arg(subfolderRelPath));
        continue;
      }
    } catch (...) {
      p_warnings.append(
          QStringLiteral("Folder '%1': creation threw exception, skipping")
              .arg(subfolderRelPath));
      continue;
    }

    // 10-11. Parse subfolder's own vx.json for folder-level metadata.
    const QString subAbsPath = absFolderPath + QStringLiteral("/") + folderEntry.name;
    const LegacyVxJson subVxJson =
        parseLegacyVxJson(subAbsPath + QStringLiteral("/vx.json"));

    if (subVxJson.createdTime > 0 || subVxJson.modifiedTime > 0) {
      try {
        m_notebookService->updateNodeTimestamps(
            p_notebookId, subfolderRelPath,
            subVxJson.createdTime, subVxJson.modifiedTime);
      } catch (...) {
        p_warnings.append(
            QStringLiteral("Folder '%1': failed to set timestamps").arg(subfolderRelPath));
      }
    }

    // 12. Apply folder visual metadata.
    if (!subVxJson.nameColor.isEmpty() || !subVxJson.backgroundColor.isEmpty()) {
      QJsonObject metaObj;
      if (!subVxJson.nameColor.isEmpty()) {
        metaObj[QStringLiteral("textColor")] = subVxJson.nameColor;
      }
      if (!subVxJson.backgroundColor.isEmpty()) {
        metaObj[QStringLiteral("backgroundColor")] = subVxJson.backgroundColor;
      }
      const QString metadataJson =
          QString::fromUtf8(QJsonDocument(metaObj).toJson(QJsonDocument::Compact));
      try {
        m_notebookService->updateFolderMetadata(
            p_notebookId, subfolderRelPath, metadataJson);
      } catch (...) {
        p_warnings.append(
            QStringLiteral("Folder '%1': failed to set visual metadata")
                .arg(subfolderRelPath));
      }
    }

    // 13. Recurse into subfolder.
    importFolder(p_notebookId, p_sourceRoot, subfolderRelPath,
                 p_destPath, p_inspection, p_warnings);
  }

  // --- Image handling: raw-copy image folder if it exists ---
  if (!p_inspection.imageFolder.isEmpty()) {
    const QString srcImageDir =
        absFolderPath + QStringLiteral("/") + p_inspection.imageFolder;
    if (QDir(srcImageDir).exists()) {
      const QString destImageDir = p_destPath + QStringLiteral("/") +
          (p_relFolderPath.isEmpty()
               ? p_inspection.imageFolder
               : p_relFolderPath + QStringLiteral("/") + p_inspection.imageFolder);
      copyDirectoryRecursively(srcImageDir, destImageDir);
    }
  }

  // --- Orphan handling: import entries not in vx.json ---
  const QDir currentDir(absFolderPath);
  const auto fsEntries =
      currentDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QFileInfo &fi : fsEntries) {
    const QString name = fi.fileName();

    // Skip if already in vx.json.
    if (vxJsonNames.contains(name)) {
      continue;
    }

    // Exclusion list.
    if (name == QStringLiteral("vx.json")) {
      continue;
    }
    if (fi.isDir()) {
      if (name == QStringLiteral("vx_notebook") ||
          name == QStringLiteral("vx_recycle_bin")) {
        continue;
      }
      if (!p_inspection.imageFolder.isEmpty() && name == p_inspection.imageFolder) {
        continue; // Already handled by image raw-copy.
      }
      if (!p_inspection.attachmentFolder.isEmpty() &&
          name == p_inspection.attachmentFolder) {
        continue; // Attachment folder is not imported as content.
      }
    }

    const QString orphanRelPath = p_relFolderPath.isEmpty()
        ? name
        : p_relFolderPath + QStringLiteral("/") + name;

    if (fi.isFile()) {
      try {
        const QString fileId =
            m_notebookService->importFile(p_notebookId, p_relFolderPath,
                                          fi.absoluteFilePath());
        if (fileId.isEmpty()) {
          p_warnings.append(
              QStringLiteral("Orphan file '%1': import failed").arg(orphanRelPath));
        }
      } catch (...) {
        p_warnings.append(
            QStringLiteral("Orphan file '%1': import threw exception").arg(orphanRelPath));
      }
    } else if (fi.isDir()) {
      try {
        const QString folderId =
            m_notebookService->createFolderPath(p_notebookId, orphanRelPath);
        if (folderId.isEmpty()) {
          p_warnings.append(
              QStringLiteral("Orphan folder '%1': creation failed").arg(orphanRelPath));
          continue;
        }
      } catch (...) {
        p_warnings.append(
            QStringLiteral("Orphan folder '%1': creation threw exception")
                .arg(orphanRelPath));
        continue;
      }
      // Recurse into orphan folder.
      importFolder(p_notebookId, p_sourceRoot, orphanRelPath,
                   p_destPath, p_inspection, p_warnings);
    }
  }
}

LegacyVxJson VNote3MigrationService::parseLegacyVxJson(const QString &p_vxJsonPath) {
  LegacyVxJson result;

  QFile file(p_vxJsonPath);
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return result;
  }

  const QByteArray data = file.readAll();
  file.close();

  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    return result;
  }

  const QJsonObject root = doc.object();

  // Parse folder-level metadata.
  {
    const LegacyTimestamp ct = parseLegacyTimestamp(root.value(QStringLiteral("created_time")));
    result.createdTime = ct.value;
  }
  {
    const LegacyTimestamp mt = parseLegacyTimestamp(root.value(QStringLiteral("modified_time")));
    result.modifiedTime = mt.value;
  }
  result.nameColor = root.value(QStringLiteral("name_color")).toString();
  result.backgroundColor = root.value(QStringLiteral("background_color")).toString();

  // Parse files array.
  const QJsonArray filesArr = root.value(QStringLiteral("files")).toArray();
  for (const QJsonValue &v : filesArr) {
    if (!v.isObject()) {
      continue;
    }
    const QJsonObject fobj = v.toObject();
    LegacyFileEntry entry;
    entry.name = fobj.value(QStringLiteral("name")).toString();
    entry.id = fobj.value(QStringLiteral("id")).toString();
    entry.attachmentFolder = fobj.value(QStringLiteral("attachment_folder")).toString();
    entry.createdTime = parseLegacyTimestamp(fobj.value(QStringLiteral("created_time")));
    entry.modifiedTime = parseLegacyTimestamp(fobj.value(QStringLiteral("modified_time")));
    entry.nameColor = fobj.value(QStringLiteral("name_color")).toString();
    entry.backgroundColor = fobj.value(QStringLiteral("background_color")).toString();

    const QJsonArray tagsArr = fobj.value(QStringLiteral("tags")).toArray();
    for (const QJsonValue &tv : tagsArr) {
      const QString tag = tv.toString();
      if (!tag.isEmpty()) {
        entry.tags.append(tag);
      }
    }

    result.files.append(entry);
  }

  // Parse folders array.
  const QJsonArray foldersArr = root.value(QStringLiteral("folders")).toArray();
  for (const QJsonValue &v : foldersArr) {
    if (!v.isObject()) {
      continue;
    }
    LegacyFolderEntry entry;
    entry.name = v.toObject().value(QStringLiteral("name")).toString();
    result.folders.append(entry);
  }

  return result;
}

QStringList VNote3MigrationService::collectAllTags(const QString &p_sourcePath) {
  QSet<QString> tagSet;

  QDirIterator it(p_sourcePath, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

  // Check the root directory itself.
  {
    const QString vxJsonPath = p_sourcePath + QStringLiteral("/vx.json");
    const LegacyVxJson vxJson = parseLegacyVxJson(vxJsonPath);
    for (const LegacyFileEntry &fe : vxJson.files) {
      for (const QString &tag : fe.tags) {
        tagSet.insert(tag);
      }
    }
  }

  // Walk subdirectories.
  while (it.hasNext()) {
    it.next();
    const QString dirPath = it.filePath();
    const QString vxJsonPath = dirPath + QStringLiteral("/vx.json");
    const LegacyVxJson vxJson = parseLegacyVxJson(vxJsonPath);
    for (const LegacyFileEntry &fe : vxJson.files) {
      for (const QString &tag : fe.tags) {
        tagSet.insert(tag);
      }
    }
  }

  return tagSet.values();
}
