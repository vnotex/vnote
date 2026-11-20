#ifndef SEARCHCORESERVICE_H
#define SEARCHCORESERVICE_H

#include <atomic>
#include <functional>

#include <QObject>
#include <QString>

#include "core/error.h"
#include "core/noncopyable.h"
#include "core/searchresulttypes.h"

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
  // @p_results: Owning output, reset on error; retains native truncation.
  // @return: Error code (Ok on success).
  Error searchFiles(const QString &p_notebookId, const QString &p_queryJson,
                    const QString &p_inputFilesJson, SearchResult *p_results) const;

  // Search file content.
  // @p_notebookId: Target notebook ID.
  // @p_queryJson: JSON search query (pattern, options, etc.).
  // @p_inputFilesJson: Optional JSON object with "files" and "folders" arrays:
  //   {"files":[...],"folders":[...]}. Empty string means no scope restriction.
  // @p_results: Owning ungrouped output, reset on error.
  // @return: Error code (Ok on success).
  Error searchContent(const QString &p_notebookId, const QString &p_queryJson,
                      const QString &p_inputFilesJson, SearchContentResult *p_results) const;

  // Search file content with cooperative cancellation support.
  // @p_notebookId: Target notebook ID.
  // @p_queryJson: JSON search query (pattern, options, etc.).
  // @p_inputFilesJson: Optional JSON object with "files" and "folders" arrays.
  // @p_cancelFlag: Atomic flag that can be set non-zero from another thread to cancel.
  //   If nullptr, behaves like searchContent().
  // @p_result: Owning ungrouped blob output, including truncation and skipped facts.
  // @return: Error code (Ok on success, Cancelled if cancelled).
  Error searchContentCancellable(const QString &p_notebookId, const QString &p_queryJson,
                                 const QString &p_inputFilesJson, std::atomic<int> *p_cancelFlag,
                                 SearchContentResult *p_result) const;

  // Callback invoked once per file-chunk batch during streaming content search.
  // MAY be invoked concurrently from multiple vxcore drain threads; the callback MUST be
  // thread-safe and MUST NOT re-enter SearchCoreService / vxcore synchronously.
  // @p_batchIndex: chunk position in input-file order (chunk 0 == files[0..batchSize)).
  // @p_totalBatches: total chunk count computed up front. A plain zero-file scan
  //   emits none; an all-encrypted scan emits one empty batch with skipped facts.
  // @p_batch: Owning ungrouped values, safe to copy/retain. Truncated is always false.
  //   The callback must not throw; no borrowed C pointer is exposed here.
  using SearchBatchCallback =
      std::function<void(int p_batchIndex, int p_totalBatches, const SearchContentResult &p_batch)>;

  // Streaming content search: delivers matches in file-chunk batches via p_onBatch instead of
  // one aggregated blob. Thin wrapper over vxcore_search_content_streaming_typed.
  // @p_notebookId: Target notebook ID.
  // @p_queryJson: JSON search query (pattern, options, etc.).
  // @p_inputFilesJson: Optional JSON object with "files" and "folders" arrays.
  // @p_batchSize: files per chunk. 0 selects vxcore's internal default.
  // @p_cancelFlag: Atomic flag that can be set non-zero from another thread to cancel.
  //   If nullptr, no cancellation. Returns ErrorCode::Cancelled if cancellation was observed.
  // @p_onBatch: Per-batch callback (see SearchBatchCallback). Must be non-null.
  // @return: Error code (Ok on success, Cancelled if cancelled). Blocks until the scan
  //   completes (the calling thread help-drains the "vxcore.search" work queue).
  Error searchContentStreaming(const QString &p_notebookId, const QString &p_queryJson,
                               const QString &p_inputFilesJson, int p_batchSize,
                               std::atomic<int> *p_cancelFlag,
                               const SearchBatchCallback &p_onBatch) const;

  // Search files by tags.
  // @p_notebookId: Target notebook ID.
  // @p_queryJson: JSON search query (tags array, options, etc.).
  // @p_inputFilesJson: Optional JSON object with "files" and "folders" arrays:
  //   {"files":[...],"folders":[...]}. Empty string means no scope restriction.
  // @p_results: Owning output, reset on error; retains native truncation.
  // @return: Error code (Ok on success).
  Error searchByTags(const QString &p_notebookId, const QString &p_queryJson,
                     const QString &p_inputFilesJson, SearchResult *p_results) const;

  // Whether normalized backend ordering guarantees Simple search replacement addresses.
  // Unreadable/malformed configuration fails closed; rg is never probed for availability.
  bool isReplacementSupported() const;

  // Non-owning access to the vxcore context (owned in main(), outlives services).
  // Used by SearchService to drain the "vxcore.search" content-search work queue.
  VxCoreContextHandle context() const { return m_context; }

private:
  // Non-owning pointer to vxcore context.
  VxCoreContextHandle m_context = nullptr;

  // Helper to convert VxCoreError to Error.
  Error vxcoreErrorToError(VxCoreError p_error, const QString &p_operation) const;
};

} // namespace vnotex

#endif // SEARCHCORESERVICE_H
