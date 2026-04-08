#ifndef SEARCHCORESERVICE_H
#define SEARCHCORESERVICE_H

#include <atomic>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "core/error.h"
#include "core/noncopyable.h"

#include <vxcore/vxcore_types.h>

namespace vnotex {

// SearchCoreService provides search operations wrapping vxcore C API.
// Uses dependency injection for VxCoreContextHandle (non-owning).
class SearchCoreService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Constructor with injected VxCoreContextHandle.
  // @p_context: Non-owning pointer to vxcore context. Must outlive SearchCoreService.
  // @p_parent: Optional QObject parent.
  explicit SearchCoreService(VxCoreContextHandle p_context, QObject *p_parent = nullptr);

  // Search files by name pattern.
  // @p_notebookId: Target notebook ID.
  // @p_queryJson: JSON search query (pattern, options, etc.).
  // @p_inputFilesJson: Optional JSON object with "files" and "folders" arrays:
  //   {"files":[...],"folders":[...]}. Empty string means no scope restriction.
  // @p_results: Output parameter for search results (the "matches" array).
  // @return: Error code (Ok on success).
  Error searchFiles(const QString &p_notebookId, const QString &p_queryJson,
                    const QString &p_inputFilesJson, QJsonArray *p_results) const;

  // Search file content.
  // @p_notebookId: Target notebook ID.
  // @p_queryJson: JSON search query (pattern, options, etc.).
  // @p_inputFilesJson: Optional JSON object with "files" and "folders" arrays:
  //   {"files":[...],"folders":[...]}. Empty string means no scope restriction.
  // @p_results: Output parameter for search results (the "matches" array).
  // @return: Error code (Ok on success).
  Error searchContent(const QString &p_notebookId, const QString &p_queryJson,
                      const QString &p_inputFilesJson, QJsonArray *p_results) const;

  // Search file content with cooperative cancellation support.
  // @p_notebookId: Target notebook ID.
  // @p_queryJson: JSON search query (pattern, options, etc.).
  // @p_inputFilesJson: Optional JSON object with "files" and "folders" arrays.
  // @p_cancelFlag: Atomic flag that can be set non-zero from another thread to cancel.
  //   If nullptr, behaves like searchContent().
  // @p_resultObj: Output parameter for the FULL vxcore JSON response
  //   (contains "matchCount", "truncated", "matches" keys).
  // @return: Error code (Ok on success, Cancelled if cancelled).
  Error searchContentCancellable(const QString &p_notebookId, const QString &p_queryJson,
                                 const QString &p_inputFilesJson, std::atomic<int> *p_cancelFlag,
                                 QJsonObject *p_resultObj) const;

  // Search files by tags.
  // @p_notebookId: Target notebook ID.
  // @p_queryJson: JSON search query (tags array, options, etc.).
  // @p_inputFilesJson: Optional JSON object with "files" and "folders" arrays:
  //   {"files":[...],"folders":[...]}. Empty string means no scope restriction.
  // @p_results: Output parameter for search results (the "matches" array).
  // @return: Error code (Ok on success).
  Error searchByTags(const QString &p_notebookId, const QString &p_queryJson,
                     const QString &p_inputFilesJson, QJsonArray *p_results) const;

private:
  // Non-owning pointer to vxcore context.
  VxCoreContextHandle m_context = nullptr;

  // Helper to convert VxCoreError to Error.
  Error vxcoreErrorToError(VxCoreError p_error, const QString &p_operation) const;

  // Parse vxcore JSON search response, extracting "matches" into p_results.
  // Frees p_json via vxcore_string_free.
  Error parseSearchResponse(char *p_json, QJsonArray *p_results) const;

  // Parse vxcore JSON search response, returning the FULL JSON object.
  // Frees p_json via vxcore_string_free.
  Error parseSearchResponseFull(char *p_json, QJsonObject *p_resultObj) const;
};

} // namespace vnotex

#endif // SEARCHCORESERVICE_H
