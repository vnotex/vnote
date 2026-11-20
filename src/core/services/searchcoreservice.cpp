#include "searchcoreservice.h"

#include <memory>

#include <QDebug>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace {

// Bridges the vxcore C batch callback back to the Qt SearchBatchCallback. A pointer to this
// struct is threaded through vxcore as the opaque userdata; it stays alive on the calling
// thread's stack until the blocking streaming call and all its callbacks have settled.
struct StreamingCallbackContext {
  const SearchCoreService::SearchBatchCallback *onBatch = nullptr;
  std::atomic<bool> failed{false};
};

// Convert while the native chunk is alive. Neither conversion nor a consumer exception
// may unwind across the C callback boundary; report failure after every callback settles.
void streamingBatchTrampoline(int p_batchIndex, int p_totalBatches,
                              const VxCoreContentSearchView *p_batch, void *p_userdata) noexcept {
  auto *ctx = static_cast<StreamingCallbackContext *>(p_userdata);
  if (ctx->failed.load(std::memory_order_relaxed)) {
    return;
  }
  try {
    const auto batch = SearchContentResult::fromView(*p_batch);
    (*ctx->onBatch)(p_batchIndex, p_totalBatches, batch);
  } catch (...) {
    ctx->failed.store(true, std::memory_order_relaxed);
  }
}

} // namespace

SearchCoreService::SearchCoreService(VxCoreContextHandle p_context, QObject *p_parent)
    : QObject(p_parent), m_context(p_context) {}

Error SearchCoreService::searchFiles(const QString &p_notebookId, const QString &p_queryJson,
                                     const QString &p_inputFilesJson,
                                     SearchResult *p_results) const {
  if (p_results) {
    *p_results = {};
  }
  if (!m_context || !p_results) {
    return Error::error(ErrorCode::InvalidArgument, "Context or results output parameter is null");
  }
  const auto notebook = p_notebookId.toUtf8();
  const auto query = p_queryJson.toUtf8();
  const auto input = p_inputFilesJson.toUtf8();
  VxCoreFileSearchResult *raw = nullptr;
  const auto err =
      vxcore_search_files_typed(m_context, notebook.constData(), query.constData(),
                                p_inputFilesJson.isEmpty() ? nullptr : input.constData(), &raw);
  const std::unique_ptr<VxCoreFileSearchResult, decltype(&vxcore_file_search_result_free)> owner(
      raw, vxcore_file_search_result_free);
  if (err != VXCORE_OK) {
    return vxcoreErrorToError(err, QStringLiteral("searchFiles"));
  }
  *p_results = SearchResult::fromFileSearchView(*vxcore_file_search_result_view(raw), p_notebookId);
  return Error::ok();
}

Error SearchCoreService::searchContent(const QString &p_notebookId, const QString &p_queryJson,
                                       const QString &p_inputFilesJson,
                                       SearchContentResult *p_results) const {
  if (p_results) {
    *p_results = {};
  }
  if (!m_context || !p_results) {
    return Error::error(ErrorCode::InvalidArgument, "Context or results output parameter is null");
  }
  const auto notebook = p_notebookId.toUtf8();
  const auto query = p_queryJson.toUtf8();
  const auto input = p_inputFilesJson.toUtf8();
  VxCoreContentSearchResult *raw = nullptr;
  const auto err =
      vxcore_search_content_typed(m_context, notebook.constData(), query.constData(),
                                  p_inputFilesJson.isEmpty() ? nullptr : input.constData(), &raw);
  const std::unique_ptr<VxCoreContentSearchResult, decltype(&vxcore_content_search_result_free)>
      owner(raw, vxcore_content_search_result_free);
  if (err != VXCORE_OK) {
    return vxcoreErrorToError(err, QStringLiteral("searchContent"));
  }
  *p_results = SearchContentResult::fromView(*vxcore_content_search_result_view(raw));
  return Error::ok();
}

Error SearchCoreService::searchByTags(const QString &p_notebookId, const QString &p_queryJson,
                                      const QString &p_inputFilesJson,
                                      SearchResult *p_results) const {
  if (p_results) {
    *p_results = {};
  }
  if (!m_context || !p_results) {
    return Error::error(ErrorCode::InvalidArgument, "Context or results output parameter is null");
  }
  const auto notebook = p_notebookId.toUtf8();
  const auto query = p_queryJson.toUtf8();
  const auto input = p_inputFilesJson.toUtf8();
  VxCoreFileSearchResult *raw = nullptr;
  const auto err =
      vxcore_search_by_tags_typed(m_context, notebook.constData(), query.constData(),
                                  p_inputFilesJson.isEmpty() ? nullptr : input.constData(), &raw);
  const std::unique_ptr<VxCoreFileSearchResult, decltype(&vxcore_file_search_result_free)> owner(
      raw, vxcore_file_search_result_free);
  if (err != VXCORE_OK) {
    return vxcoreErrorToError(err, QStringLiteral("searchByTags"));
  }
  *p_results = SearchResult::fromFileSearchView(*vxcore_file_search_result_view(raw), p_notebookId);
  return Error::ok();
}

Error SearchCoreService::vxcoreErrorToError(VxCoreError p_error, const QString &p_operation) const {
  ErrorCode code;
  switch (p_error) {
  case VXCORE_OK:
    return Error::ok();
  case VXCORE_ERR_INVALID_PARAM:
    code = ErrorCode::InvalidArgument;
    break;
  case VXCORE_ERR_NULL_POINTER:
    code = ErrorCode::InvalidArgument;
    break;
  case VXCORE_ERR_NOT_FOUND:
    code = ErrorCode::FileMissingOnDisk;
    break;
  case VXCORE_ERR_ALREADY_EXISTS:
    code = ErrorCode::FileExistsOnCreate;
    break;
  case VXCORE_ERR_IO:
    code = ErrorCode::FailToReadFile;
    break;
  case VXCORE_ERR_DATABASE:
    code = ErrorCode::FailToReadFile;
    break;
  case VXCORE_ERR_JSON_PARSE:
    code = ErrorCode::InvalidArgument;
    break;
  case VXCORE_ERR_JSON_SERIALIZE:
    code = ErrorCode::FailToWriteFile;
    break;
  case VXCORE_ERR_PERMISSION_DENIED:
    code = ErrorCode::FailToReadFile;
    break;
  case VXCORE_ERR_CANCELLED:
    code = ErrorCode::Cancelled;
    break;
  default:
    code = ErrorCode::InvalidArgument;
    break;
  }

  const QString msg = QString::fromUtf8(vxcore_error_message(p_error));
  return Error::error(code, QStringLiteral("%1 failed: %2").arg(p_operation, msg));
}

Error SearchCoreService::searchContentCancellable(const QString &p_notebookId,
                                                  const QString &p_queryJson,
                                                  const QString &p_inputFilesJson,
                                                  std::atomic<int> *p_cancelFlag,
                                                  SearchContentResult *p_result) const {
  if (p_result) {
    *p_result = {};
  }
  if (!m_context || !p_result) {
    return Error::error(ErrorCode::InvalidArgument, "Context or result output parameter is null");
  }
  const auto notebook = p_notebookId.toUtf8();
  const auto query = p_queryJson.toUtf8();
  const auto input = p_inputFilesJson.toUtf8();
  volatile int *cancel = p_cancelFlag ? reinterpret_cast<volatile int *>(p_cancelFlag) : nullptr;
  VxCoreContentSearchResult *raw = nullptr;
  const auto err = vxcore_search_content_ex_typed(
      m_context, notebook.constData(), query.constData(),
      p_inputFilesJson.isEmpty() ? nullptr : input.constData(), cancel, &raw);
  const std::unique_ptr<VxCoreContentSearchResult, decltype(&vxcore_content_search_result_free)>
      owner(raw, vxcore_content_search_result_free);
  if (err != VXCORE_OK) {
    return vxcoreErrorToError(err, QStringLiteral("searchContentCancellable"));
  }
  *p_result = SearchContentResult::fromView(*vxcore_content_search_result_view(raw));
  return Error::ok();
}

Error SearchCoreService::searchContentStreaming(const QString &p_notebookId,
                                                const QString &p_queryJson,
                                                const QString &p_inputFilesJson, int p_batchSize,
                                                std::atomic<int> *p_cancelFlag,
                                                const SearchBatchCallback &p_onBatch) const {
  qDebug() << "SearchCoreService::searchContentStreaming: notebookId:" << p_notebookId
           << "batchSize:" << p_batchSize << "hasCancelFlag:" << (p_cancelFlag != nullptr);

  if (!m_context) {
    qWarning() << "SearchCoreService::searchContentStreaming: context is null";
    return Error::error(ErrorCode::InvalidArgument, "Context is null");
  }

  if (!p_onBatch) {
    qWarning() << "SearchCoreService::searchContentStreaming: batch callback is null";
    return Error::error(ErrorCode::InvalidArgument, "Batch callback is null");
  }

  // Local QByteArray variables ensure data lives until vxcore call completes.
  QByteArray notebookUtf8 = p_notebookId.toUtf8();
  QByteArray queryUtf8 = p_queryJson.toUtf8();
  QByteArray inputUtf8 = p_inputFilesJson.toUtf8();
  const char *notebookCStr = notebookUtf8.constData();
  const char *queryCStr = queryUtf8.constData();
  const char *inputCStr = p_inputFilesJson.isEmpty() ? nullptr : inputUtf8.constData();

  // Cast std::atomic<int>* to volatile int* at the C boundary.
  volatile int *cancelFlagPtr =
      p_cancelFlag ? reinterpret_cast<volatile int *>(p_cancelFlag) : nullptr;

  // The typed streaming call blocks until every callback settles, so cbCtx remains alive.
  StreamingCallbackContext cbCtx;
  cbCtx.onBatch = &p_onBatch;

  VxCoreError err = vxcore_search_content_streaming_typed(
      m_context, notebookCStr, queryCStr, inputCStr, p_batchSize, streamingBatchTrampoline, &cbCtx,
      cancelFlagPtr);

  if (err != VXCORE_OK) {
    qWarning() << "SearchCoreService::searchContentStreaming: vxcore_search_content_streaming "
                  "failed, error:"
               << err;
    return vxcoreErrorToError(err, QStringLiteral("searchContentStreaming"));
  }
  if (cbCtx.failed.load(std::memory_order_relaxed)) {
    return Error::error(ErrorCode::InvalidArgument, "Search batch conversion or callback failed");
  }

  qDebug() << "SearchCoreService::searchContentStreaming: vxcore call succeeded";
  return Error::ok();
}
