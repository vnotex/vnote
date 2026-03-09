#ifndef SEARCHSERVICE_H
#define SEARCHSERVICE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "core/error.h"
#include "core/noncopyable.h"

#include <vxcore/vxcore_types.h>

namespace vnotex::core {

// SearchService provides search operations wrapping vxcore C API.
// Uses dependency injection for VxCoreContextHandle (non-owning).
class SearchService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Constructor with injected VxCoreContextHandle.
  // @p_context: Non-owning pointer to vxcore context. Must outlive SearchService.
  // @p_parent: Optional QObject parent.
  explicit SearchService(VxCoreContextHandle p_context, QObject *p_parent = nullptr);

  // Search files by name pattern.
  // @p_notebookId: Target notebook ID.
  // @p_queryJson: JSON search query (pattern, options, etc.).
  // @p_inputFilesJson: Optional JSON array of file paths to limit search scope.
  // @p_results: Output parameter for search results.
  // @return: Error code (Ok on success).
  Error searchFiles(const QString &p_notebookId,
                    const QString &p_queryJson,
                    const QString &p_inputFilesJson,
                    QJsonArray *p_results) const;

  // Search file content.
  // @p_notebookId: Target notebook ID.
  // @p_queryJson: JSON search query (pattern, options, etc.).
  // @p_inputFilesJson: Optional JSON array of file paths to limit search scope.
  // @p_results: Output parameter for search results.
  // @return: Error code (Ok on success).
  Error searchContent(const QString &p_notebookId,
                      const QString &p_queryJson,
                      const QString &p_inputFilesJson,
                      QJsonArray *p_results) const;

  // Search files by tags.
  // @p_notebookId: Target notebook ID.
  // @p_queryJson: JSON search query (tags array, options, etc.).
  // @p_inputFilesJson: Optional JSON array of file paths to limit search scope.
  // @p_results: Output parameter for search results.
  // @return: Error code (Ok on success).
  Error searchByTags(const QString &p_notebookId,
                     const QString &p_queryJson,
                     const QString &p_inputFilesJson,
                     QJsonArray *p_results) const;

private:
  // Non-owning pointer to vxcore context.
  VxCoreContextHandle m_context = nullptr;

  // Helper to convert VxCoreError to Error.
  Error vxcoreErrorToError(VxCoreError p_error, const QString &p_operation) const;

  // Helper to convert QString to C string (nullptr for empty).
  static const char *qstringToCStr(const QString &p_str);
};

} // namespace vnotex::core

#endif // SEARCHSERVICE_H
