#include "searchcoreservice.h"

#include <QDebug>

#include <vxcore/vxcore.h>

using namespace vnotex;

SearchCoreService::SearchCoreService(VxCoreContextHandle p_context, QObject *p_parent)
  : QObject(p_parent), m_context(p_context) {
}

Error SearchCoreService::searchFiles(const QString &p_notebookId,
                                 const QString &p_queryJson,
                                 const QString &p_inputFilesJson,
                                 QJsonArray *p_results) const {
  qDebug() << "SearchCoreService::searchFiles: notebookId:" << p_notebookId;

  if (!m_context) {
    qWarning() << "SearchCoreService::searchFiles: context is null";
    return Error::error(ErrorCode::InvalidArgument, "Context is null");
  }

  if (!p_results) {
    qWarning() << "SearchCoreService::searchFiles: results output parameter is null";
    return Error::error(ErrorCode::InvalidArgument, "Results output parameter is null");
  }

  // Local QByteArray variables ensure data lives until vxcore call completes.
  QByteArray notebookUtf8 = p_notebookId.toUtf8();
  QByteArray queryUtf8 = p_queryJson.toUtf8();
  QByteArray inputUtf8 = p_inputFilesJson.toUtf8();
  const char *notebookCStr = notebookUtf8.constData();
  const char *queryCStr = queryUtf8.constData();
  const char *inputCStr = p_inputFilesJson.isEmpty() ? nullptr : inputUtf8.constData();

  char *json = nullptr;
  VxCoreError err = vxcore_search_files(m_context,
                                        notebookCStr,
                                        queryCStr,
                                        inputCStr,
                                        &json);

  if (err != VXCORE_OK) {
    qWarning() << "SearchCoreService::searchFiles: vxcore_search_files failed, error:" << err;
    return vxcoreErrorToError(err, QStringLiteral("searchFiles"));
  }

  qDebug() << "SearchCoreService::searchFiles: vxcore call succeeded, parsing response";
  return parseSearchResponse(json, p_results);
}

Error SearchCoreService::searchContent(const QString &p_notebookId,
                                   const QString &p_queryJson,
                                   const QString &p_inputFilesJson,
                                   QJsonArray *p_results) const {
  qDebug() << "SearchCoreService::searchContent: notebookId:" << p_notebookId;

  if (!m_context) {
    qWarning() << "SearchCoreService::searchContent: context is null";
    return Error::error(ErrorCode::InvalidArgument, "Context is null");
  }

  if (!p_results) {
    qWarning() << "SearchCoreService::searchContent: results output parameter is null";
    return Error::error(ErrorCode::InvalidArgument, "Results output parameter is null");
  }

  // Local QByteArray variables ensure data lives until vxcore call completes.
  QByteArray notebookUtf8 = p_notebookId.toUtf8();
  QByteArray queryUtf8 = p_queryJson.toUtf8();
  QByteArray inputUtf8 = p_inputFilesJson.toUtf8();
  const char *notebookCStr = notebookUtf8.constData();
  const char *queryCStr = queryUtf8.constData();
  const char *inputCStr = p_inputFilesJson.isEmpty() ? nullptr : inputUtf8.constData();

  char *json = nullptr;
  VxCoreError err = vxcore_search_content(m_context,
                                          notebookCStr,
                                          queryCStr,
                                          inputCStr,
                                          &json);

  if (err != VXCORE_OK) {
    qWarning() << "SearchCoreService::searchContent: vxcore_search_content failed, error:" << err;
    return vxcoreErrorToError(err, QStringLiteral("searchContent"));
  }

  qDebug() << "SearchCoreService::searchContent: vxcore call succeeded, parsing response";
  return parseSearchResponse(json, p_results);
}

Error SearchCoreService::searchByTags(const QString &p_notebookId,
                                  const QString &p_queryJson,
                                  const QString &p_inputFilesJson,
                                  QJsonArray *p_results) const {
  qDebug() << "SearchCoreService::searchByTags: notebookId:" << p_notebookId;

  if (!m_context) {
    qWarning() << "SearchCoreService::searchByTags: context is null";
    return Error::error(ErrorCode::InvalidArgument, "Context is null");
  }

  if (!p_results) {
    qWarning() << "SearchCoreService::searchByTags: results output parameter is null";
    return Error::error(ErrorCode::InvalidArgument, "Results output parameter is null");
  }

  // Local QByteArray variables ensure data lives until vxcore call completes.
  QByteArray notebookUtf8 = p_notebookId.toUtf8();
  QByteArray queryUtf8 = p_queryJson.toUtf8();
  QByteArray inputUtf8 = p_inputFilesJson.toUtf8();
  const char *notebookCStr = notebookUtf8.constData();
  const char *queryCStr = queryUtf8.constData();
  const char *inputCStr = p_inputFilesJson.isEmpty() ? nullptr : inputUtf8.constData();

  char *json = nullptr;
  VxCoreError err = vxcore_search_by_tags(m_context,
                                          notebookCStr,
                                          queryCStr,
                                          inputCStr,
                                          &json);

  if (err != VXCORE_OK) {
    qWarning() << "SearchCoreService::searchByTags: vxcore_search_by_tags failed, error:" << err;
    return vxcoreErrorToError(err, QStringLiteral("searchByTags"));
  }

  qDebug() << "SearchCoreService::searchByTags: vxcore call succeeded, parsing response";
  return parseSearchResponse(json, p_results);
}

Error SearchCoreService::parseSearchResponse(char *p_json, QJsonArray *p_results) const {
  if (p_json) {
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(p_json));
    vxcore_string_free(p_json);
    if (doc.isObject()) {
      QJsonObject obj = doc.object();

      // Preserve matchCount and truncated from the vxcore response.
      const QString matchCountKey = QStringLiteral("matchCount");
      const QString truncatedKey = QStringLiteral("truncated");
      const QString matchesKey = QStringLiteral("matches");
      m_lastMatchCount = obj.value(matchCountKey).toInt(0);
      m_lastTruncated = obj.value(truncatedKey).toBool(false);

      if (obj.contains(matchesKey) && obj[matchesKey].isArray()) {
        *p_results = obj[matchesKey].toArray();
      } else {
        *p_results = QJsonArray();
      }

      qDebug() << "SearchCoreService::parseSearchResponse: matchCount:" << m_lastMatchCount
               << "truncated:" << m_lastTruncated
               << "resultsArraySize:" << p_results->size();
    } else {
      qWarning() << "SearchCoreService::parseSearchResponse: invalid JSON object response";
      return Error::error(ErrorCode::InvalidArgument, "Invalid JSON object response");
    }
  } else {
    qDebug() << "SearchCoreService::parseSearchResponse: null JSON response, returning empty";
    m_lastMatchCount = 0;
    m_lastTruncated = false;
    *p_results = QJsonArray();
  }

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
                                                   QJsonObject *p_resultObj) const {
  qDebug() << "SearchCoreService::searchContentCancellable: notebookId:" << p_notebookId
           << "hasCancelFlag:" << (p_cancelFlag != nullptr);

  if (!m_context) {
    qWarning() << "SearchCoreService::searchContentCancellable: context is null";
    return Error::error(ErrorCode::InvalidArgument, "Context is null");
  }

  if (!p_resultObj) {
    qWarning() << "SearchCoreService::searchContentCancellable: result output parameter is null";
    return Error::error(ErrorCode::InvalidArgument, "Result output parameter is null");
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

  char *json = nullptr;
  VxCoreError err = vxcore_search_content_ex(m_context,
                                             notebookCStr,
                                             queryCStr,
                                             inputCStr,
                                             cancelFlagPtr,
                                             &json);

  if (err != VXCORE_OK) {
    qWarning() << "SearchCoreService::searchContentCancellable: vxcore_search_content_ex failed, error:" << err;
    return vxcoreErrorToError(err, QStringLiteral("searchContentCancellable"));
  }

  qDebug() << "SearchCoreService::searchContentCancellable: vxcore call succeeded, parsing response";
  return parseSearchResponseFull(json, p_resultObj);
}

Error SearchCoreService::parseSearchResponseFull(char *p_json, QJsonObject *p_resultObj) const {
  if (p_json) {
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(p_json));
    vxcore_string_free(p_json);
    if (doc.isObject()) {
      *p_resultObj = doc.object();

      // Cache matchCount and truncated from the response.
      const QString matchCountKey = QStringLiteral("matchCount");
      const QString truncatedKey = QStringLiteral("truncated");
      m_lastMatchCount = p_resultObj->value(matchCountKey).toInt(0);
      m_lastTruncated = p_resultObj->value(truncatedKey).toBool(false);

      qDebug() << "SearchCoreService::parseSearchResponseFull: matchCount:" << m_lastMatchCount
               << "truncated:" << m_lastTruncated;
    } else {
      qWarning() << "SearchCoreService::parseSearchResponseFull: invalid JSON object response";
      return Error::error(ErrorCode::InvalidArgument, "Invalid JSON object response");
    }
  } else {
    qDebug() << "SearchCoreService::parseSearchResponseFull: null JSON response, returning empty";
    m_lastMatchCount = 0;
    m_lastTruncated = false;
    *p_resultObj = QJsonObject();
  }

  return Error::ok();
}
