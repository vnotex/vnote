#ifndef FILETYPECORESERVICE_H
#define FILETYPECORESERVICE_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <vxcore/vxcore.h>

#include <core/buffer/filetypehelper.h>
#include <core/noncopyable.h>

namespace vnotex {

// Service for file type detection and management.
// Replaces legacy FileTypeHelper singleton with DI-compatible service.
// Reads file types from vxcore configuration (user modifies vxcore.json to customize).
// No caching - queries vxcore on each request for simplicity.
class FileTypeCoreService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Constructor receives VxCoreContextHandle and locale via dependency injection.
  // @p_locale: Locale string (e.g., "zh_CN", "en_US") for display name lookup.
  explicit FileTypeCoreService(VxCoreContextHandle p_context, const QString &p_locale,
                           QObject *p_parent = nullptr);

  // Get file type by file path (uses suffix detection).
  FileType getFileType(const QString &p_filePath) const;

  // Get file type by internal type name (calls vxcore_filetype_get_by_name).
  FileType getFileTypeByName(const QString &p_typeName) const;

  // Get file type by file suffix (calls vxcore_filetype_get_by_suffix).
  FileType getFileTypeBySuffix(const QString &p_suffix) const;

  // Get all registered file types (calls vxcore_filetype_list).
  QVector<FileType> getAllFileTypes() const;

  // Set all file types (replaces entire configuration).
  // Returns true on success, false on validation/save failure.
  bool setAllFileTypes(const QVector<FileType> &p_types);

private:
  // Parse a single file type from JSON object.
  FileType parseFileType(const QJsonObject &p_obj) const;

  // Parse a single file type from JSON string.
  FileType parseFileTypeFromJson(const char *p_json) const;

  // Parse multiple file types from JSON string (array).
  QVector<FileType> parseFileTypesFromJson(const char *p_json) const;

  // Non-owning handle to vxcore context.
  VxCoreContextHandle m_context = nullptr;

  // Locale string for display name lookup (e.g., "zh_CN", "en_US").
  QString m_locale;
};

} // namespace vnotex

#endif // FILETYPECORESERVICE_H
