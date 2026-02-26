#ifndef FILETYPESERVICE_H
#define FILETYPESERVICE_H

#include <QMap>
#include <QObject>
#include <QString>
#include <QVector>

#include <core/buffer/filetypehelper.h>
#include <core/noncopyable.h>

namespace vnotex {

class ConfigMgr2;

// Service for file type detection and management.
// Replaces legacy FileTypeHelper singleton with DI-compatible service.
// Depends on ConfigMgr2 for user-configured suffix mappings.
class FileTypeService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Constructor receives ConfigMgr2 via dependency injection.
  explicit FileTypeService(ConfigMgr2 *p_configMgr, QObject *p_parent = nullptr);

  // Get file type by file path (uses suffix detection).
  const FileType &getFileType(const QString &p_filePath) const;

  // Get file type by enum type value.
  const FileType &getFileType(int p_type) const;

  // Get file type by internal type name.
  const FileType &getFileTypeByName(const QString &p_typeName) const;

  // Get file type by file suffix.
  const FileType &getFileTypeBySuffix(const QString &p_suffix) const;

  // Get all registered file types.
  const QVector<FileType> &getAllFileTypes() const;

  // Check if file path matches specified type.
  bool checkFileType(const QString &p_filePath, int p_type) const;

  // Reload file types from configuration.
  void reload();

  // System default program identifier.
  static QString s_systemDefaultProgram;

private:
  // Setup built-in file types with configured suffixes.
  void setupBuiltInTypes();

  // Build suffix-to-type lookup map.
  void setupSuffixTypeMap();

  // Non-owning pointer to ConfigMgr2.
  ConfigMgr2 *m_configMgr = nullptr;

  // Built-in file types indexed by Type enum.
  QVector<FileType> m_fileTypes;

  // Suffix to file type index mapping.
  QMap<QString, int> m_suffixTypeMap;
};

} // namespace vnotex

#endif // FILETYPESERVICE_H
