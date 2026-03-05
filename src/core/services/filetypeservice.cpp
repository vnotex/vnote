#include "filetypeservice.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <vxcore/vxcore.h>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <utils/fileutils.h>

using namespace vnotex;
QString FileTypeService::s_systemDefaultProgram = QStringLiteral("System");

FileTypeService::FileTypeService(VxCoreContextHandle p_context, ConfigMgr2 *p_configMgr,
                                 QObject *p_parent)
    : QObject(p_parent), m_context(p_context), m_configMgr(p_configMgr) {
  reload();
}

void FileTypeService::reload() {
  setupBuiltInTypes();
  setupSuffixTypeMap();
}

void FileTypeService::setupBuiltInTypes() {
  m_fileTypes.clear();
  const auto &coreConfig = m_configMgr->getCoreConfig();

  // Call vxcore API to get 5 default types
  char *json_str = nullptr;
  VxCoreError err = vxcore_filetype_list(m_context, &json_str);

  if (err != VXCORE_OK || !json_str) {
    qWarning() << "vxcore_filetype_list failed:" << QString::fromUtf8(vxcore_error_message(err));
    // FALLBACK: Use hardcoded defaults
    {
      FileType type;
      type.m_type = FileType::Markdown;
      type.m_typeName = QStringLiteral("Markdown");
      type.m_displayName = QCoreApplication::translate("vnotex::Buffer", "Markdown");

      auto suffixes = coreConfig.findFileTypeSuffix(type.m_typeName);
      if (suffixes && !suffixes->isEmpty()) {
        type.m_suffixes = *suffixes;
      } else {
        type.m_suffixes << QStringLiteral("md") << QStringLiteral("mkd") << QStringLiteral("rmd")
                        << QStringLiteral("markdown");
      }

      m_fileTypes.push_back(type);
    }

    {
      FileType type;
      type.m_type = FileType::Text;
      type.m_typeName = QStringLiteral("Text");
      type.m_displayName = QCoreApplication::translate("vnotex::Buffer", "Text");

      auto suffixes = coreConfig.findFileTypeSuffix(type.m_typeName);
      if (suffixes && !suffixes->isEmpty()) {
        type.m_suffixes = *suffixes;
      } else {
        type.m_suffixes << QStringLiteral("txt") << QStringLiteral("text") << QStringLiteral("log");
      }

      m_fileTypes.push_back(type);
    }

    {
      FileType type;
      type.m_type = FileType::Pdf;
      type.m_typeName = QStringLiteral("PDF");
      type.m_displayName = QCoreApplication::translate("vnotex::Buffer", "Portable Document Format");
      type.m_isNewable = false;

      auto suffixes = coreConfig.findFileTypeSuffix(type.m_typeName);
      if (suffixes && !suffixes->isEmpty()) {
        type.m_suffixes = *suffixes;
      } else {
        type.m_suffixes << QStringLiteral("pdf");
      }

      m_fileTypes.push_back(type);
    }

    {
      FileType type;
      type.m_type = FileType::MindMap;
      type.m_typeName = QStringLiteral("MindMap");
      type.m_displayName = QCoreApplication::translate("vnotex::Buffer", "Mind Map");

      auto suffixes = coreConfig.findFileTypeSuffix(type.m_typeName);
      if (suffixes && !suffixes->isEmpty()) {
        type.m_suffixes = *suffixes;
      } else {
        type.m_suffixes << QStringLiteral("emind");
      }

      m_fileTypes.push_back(type);
    }

    {
      FileType type;
      type.m_type = FileType::Others;
      type.m_typeName = QStringLiteral("Others");
      type.m_displayName = QCoreApplication::translate("vnotex::Buffer", "Others");
      m_fileTypes.push_back(type);
    }
    return;
  }

  // Parse JSON response
  QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json_str));
  vxcore_string_free(json_str);

  if (!doc.isArray()) {
    qWarning() << "Invalid JSON from vxcore_filetype_list";
    return;
  }

  // Populate m_fileTypes from JSON
  QJsonArray typesArray = doc.array();
  for (const auto &typeVal : typesArray) {
    QJsonObject typeObj = typeVal.toObject();

    FileType type;

    // Map vxcore typeId to Qt enum (0=Markdown, 1=Text, 2=Pdf, 3=MindMap, 4=Others)
    int typeId = typeObj["typeId"].toInt();
    type.m_type = static_cast<FileType::Type>(typeId);

    type.m_typeName = typeObj["name"].toString();
    type.m_isNewable = typeObj["isNewable"].toBool();

    // KEEP Qt translation (do NOT use vxcore displayName)
    type.m_displayName =
        QCoreApplication::translate("vnotex::Buffer", type.m_typeName.toUtf8().constData());

    // Get default suffixes from vxcore
    QJsonArray suffixesArray = typeObj["suffixes"].toArray();
    QStringList defaultSuffixes;
    for (const auto &suffix : suffixesArray) {
      defaultSuffixes << suffix.toString();
    }

    // KEEP user config override logic
    auto suffixes = coreConfig.findFileTypeSuffix(type.m_typeName);
    if (suffixes && !suffixes->isEmpty()) {
      type.m_suffixes = *suffixes; // User override wins
    } else {
      type.m_suffixes = defaultSuffixes; // Use vxcore defaults
    }

    m_fileTypes.push_back(type);
  }
}

const FileType &FileTypeService::getFileType(const QString &p_filePath) const {
  Q_ASSERT(!p_filePath.isEmpty());

  QFileInfo fi(p_filePath);
  auto suffix = fi.suffix().toLower();
  auto it = m_suffixTypeMap.find(suffix);
  if (it != m_suffixTypeMap.end()) {
    return m_fileTypes.at(it.value());
  }

  // Treat all unknown text files as plain text files.
  if (FileUtils::isText(p_filePath)) {
    return m_fileTypes[FileType::Text];
  }

  return m_fileTypes[FileType::Others];
}

const FileType &FileTypeService::getFileTypeBySuffix(const QString &p_suffix) const {
  auto it = m_suffixTypeMap.find(p_suffix.toLower());
  if (it != m_suffixTypeMap.end()) {
    return m_fileTypes.at(it.value());
  } else {
    return m_fileTypes[FileType::Others];
  }
}

void FileTypeService::setupSuffixTypeMap() {
  m_suffixTypeMap.clear();

  for (int i = 0; i < m_fileTypes.size(); ++i) {
    for (const auto &suffix : m_fileTypes[i].m_suffixes) {
      if (m_suffixTypeMap.contains(suffix)) {
        qWarning() << "suffix conflicts detected" << suffix << m_fileTypes[i].m_type;
      }
      m_suffixTypeMap.insert(suffix, i);
    }
  }
}

const QVector<FileType> &FileTypeService::getAllFileTypes() const { return m_fileTypes; }

const FileType &FileTypeService::getFileType(int p_type) const {
  if (p_type >= m_fileTypes.size()) {
    p_type = FileType::Others;
  }
  return m_fileTypes[p_type];
}

bool FileTypeService::checkFileType(const QString &p_filePath, int p_type) const {
  return getFileType(p_filePath).m_type == p_type;
}

const FileType &FileTypeService::getFileTypeByName(const QString &p_typeName) const {
  for (const auto &ft : m_fileTypes) {
    if (ft.m_typeName == p_typeName) {
      return ft;
    }
  }

  Q_ASSERT(false);
  return m_fileTypes[FileType::Others];
}
