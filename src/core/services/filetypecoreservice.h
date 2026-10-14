#ifndef FILETYPECORESERVICE_H
#define FILETYPECORESERVICE_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include <vxcore/vxcore.h>

#include <core/noncopyable.h>
#include <core/services/filetype.h>

namespace vnotex {

// Service for file type detection and management.
// Replaces legacy FileTypeHelper singleton with DI-compatible service.
// Projects the saved vxcore registry plus an absent built-in Image default without writing it.
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

  // Get a saved type by name, or the application Image default when absent.
  FileType getFileTypeByName(const QString &p_typeName) const;

  // Saved suffix mappings take precedence over the application Image default.
  FileType getFileTypeBySuffix(const QString &p_suffix) const;

  // Get the successful saved registry with an absent Image default projected into it.
  // Only setAllFileTypes() persists that default (for example, Settings Apply).
  QVector<FileType> getAllFileTypes() const;

  // Set all file types (replaces entire configuration).
  // Returns true on success, false on validation/save failure.
  bool setAllFileTypes(const QVector<FileType> &p_types);

private:
  // Parse a single file type from JSON object.
  FileType parseFileType(const QJsonObject &p_obj) const;

  // Parse a single file type from JSON string.
  FileType parseFileTypeFromJson(const char *p_json) const;

  // Build the Image default from installed decoders, excluding saved associations.
  FileType defaultImageFileType(const QVector<FileType> &p_types) const;

  // Parse and augment a successful saved registry JSON array.
  QVector<FileType> parseFileTypesFromJson(const char *p_json) const;

  // Non-owning handle to vxcore context.
  VxCoreContextHandle m_context = nullptr;

  // Locale string for display name lookup (e.g., "zh_CN", "en_US").
  QString m_locale;
};

} // namespace vnotex

#endif // FILETYPECORESERVICE_H
