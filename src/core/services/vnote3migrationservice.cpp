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

  // 4. Recursively enumerate the source tree.
  const QDir sourceDir(p_sourcePath);
  QDirIterator it(p_sourcePath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);

  while (it.hasNext()) {
    it.next();
    const QFileInfo fi = it.fileInfo();
    const QString relPath = sourceDir.relativeFilePath(fi.absoluteFilePath());

    // 5. Check exclusion: skip vx_notebook and vx_recycle_bin subtrees.
    if (relPath.startsWith(QStringLiteral("vx_notebook")) ||
        relPath.startsWith(QStringLiteral("vx_recycle_bin"))) {
      continue;
    }

    if (fi.isDir()) {
      // Create the folder path in the destination notebook.
      const QString folderId = m_notebookService->createFolderPath(notebookId, relPath);
      if (folderId.isEmpty()) {
        result.errorMessage = QStringLiteral("Failed to create folder: %1").arg(relPath);
        m_notebookService->closeNotebook(notebookId);
        QDir(p_destPath).removeRecursively();
        return result;
      }
    } else if (fi.isFile()) {
      // Exclude vx.json files.
      if (fi.fileName() == QStringLiteral("vx.json")) {
        continue;
      }

      // Determine the parent folder relative path.
      const QString parentRelPath = sourceDir.relativeFilePath(fi.absolutePath());
      const QString destFolder = (parentRelPath == QStringLiteral(".")) ? QString() : parentRelPath;

      const QString fileId =
          m_notebookService->importFile(notebookId, destFolder, fi.absoluteFilePath());
      if (fileId.isEmpty()) {
        result.errorMessage = QStringLiteral("Failed to import file: %1").arg(relPath);
        m_notebookService->closeNotebook(notebookId);
        QDir(p_destPath).removeRecursively();
        return result;
      }
    }
  }

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
