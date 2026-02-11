#include "searchservice.h"

#include <vxcore/vxcore.h>

using namespace vnotex;

SearchService::SearchService(VxCoreContextHandle p_context, QObject *p_parent)
  : QObject(p_parent), m_context(p_context) {
}

Error SearchService::searchFiles(const QString &p_notebookId,
                                 const QString &p_queryJson,
                                 const QString &p_inputFilesJson,
                                 QJsonArray *p_results) const {
  if (!m_context) {
    return Error::error(ErrorCode::InvalidArgument, "Context is null");
  }

  if (!p_results) {
    return Error::error(ErrorCode::InvalidArgument, "Results output parameter is null");
  }

  char *json = nullptr;
  VxCoreError err = vxcore_search_files(m_context,
                                        p_notebookId.toUtf8().constData(),
                                        p_queryJson.toUtf8().constData(),
                                        qstringToCStr(p_inputFilesJson),
                                        &json);

  if (err != VXCORE_OK) {
    return vxcoreErrorToError(err, QStringLiteral("searchFiles"));
  }

  if (json) {
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
    vxcore_string_free(json);
    if (doc.isObject()) {
      QJsonObject obj = doc.object();
      if (obj.contains("matches") && obj["matches"].isArray()) {
        *p_results = obj["matches"].toArray();
      } else {
        *p_results = QJsonArray();
      }
    } else {
      return Error::error(ErrorCode::InvalidArgument, "Invalid JSON object response");
    }
  } else {
    *p_results = QJsonArray();
  }

  return Error::ok();
}

Error SearchService::searchContent(const QString &p_notebookId,
                                   const QString &p_queryJson,
                                   const QString &p_inputFilesJson,
                                   QJsonArray *p_results) const {
  if (!m_context) {
    return Error::error(ErrorCode::InvalidArgument, "Context is null");
  }

  if (!p_results) {
    return Error::error(ErrorCode::InvalidArgument, "Results output parameter is null");
  }

  char *json = nullptr;
  VxCoreError err = vxcore_search_content(m_context,
                                          p_notebookId.toUtf8().constData(),
                                          p_queryJson.toUtf8().constData(),
                                          qstringToCStr(p_inputFilesJson),
                                          &json);

  if (err != VXCORE_OK) {
    return vxcoreErrorToError(err, QStringLiteral("searchContent"));
  }

  if (json) {
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
    vxcore_string_free(json);
    if (doc.isObject()) {
      QJsonObject obj = doc.object();
      if (obj.contains("matches") && obj["matches"].isArray()) {
        *p_results = obj["matches"].toArray();
      } else {
        *p_results = QJsonArray();
      }
    } else {
      return Error::error(ErrorCode::InvalidArgument, "Invalid JSON object response");
    }
  } else {
    *p_results = QJsonArray();
  }

  return Error::ok();
}

Error SearchService::searchByTags(const QString &p_notebookId,
                                  const QString &p_queryJson,
                                  const QString &p_inputFilesJson,
                                  QJsonArray *p_results) const {
  if (!m_context) {
    return Error::error(ErrorCode::InvalidArgument, "Context is null");
  }

  if (!p_results) {
    return Error::error(ErrorCode::InvalidArgument, "Results output parameter is null");
  }

  char *json = nullptr;
  VxCoreError err = vxcore_search_by_tags(m_context,
                                          p_notebookId.toUtf8().constData(),
                                          p_queryJson.toUtf8().constData(),
                                          qstringToCStr(p_inputFilesJson),
                                          &json);

  if (err != VXCORE_OK) {
    return vxcoreErrorToError(err, QStringLiteral("searchByTags"));
  }

  if (json) {
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json));
    vxcore_string_free(json);
    if (doc.isObject()) {
      QJsonObject obj = doc.object();
      if (obj.contains("matches") && obj["matches"].isArray()) {
        *p_results = obj["matches"].toArray();
      } else {
        *p_results = QJsonArray();
      }
    } else {
      return Error::error(ErrorCode::InvalidArgument, "Invalid JSON object response");
    }
  } else {
    *p_results = QJsonArray();
  }

  return Error::ok();
}

Error SearchService::vxcoreErrorToError(VxCoreError p_error, const QString &p_operation) const {
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
  default:
    code = ErrorCode::InvalidArgument;
    break;
  }

  const QString msg = QString::fromUtf8(vxcore_error_message(p_error));
  return Error::error(code, QStringLiteral("%1 failed: %2").arg(p_operation, msg));
}

const char *SearchService::qstringToCStr(const QString &p_str) {
  return p_str.isEmpty() ? nullptr : p_str.toUtf8().constData();
}
