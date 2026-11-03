#include "apiserver.h"

#include "apiaudit.h"
#include "apiauth.h"
#include "apioperations.h"
#include "iapibackend.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QThread>
#include <QUuid>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <http/httpserver.h>
#include <mutex>
#include <optional>

using namespace vnotex;

namespace {
constexpr int c_deadlineSeconds = 20;

HttpResponse problem(const ApiError &p_error, const QString &p_requestId) {
  HttpResponse response;
  response.status = p_error.status;
  response.contentType = "application/problem+json";
  response.body = QJsonDocument(QJsonObject{{QStringLiteral("type"),
                                             QStringLiteral("urn:vnote:api:error:") + p_error.code},
                                            {QStringLiteral("status"), p_error.status},
                                            {QStringLiteral("code"), p_error.code},
                                            {QStringLiteral("title"), p_error.message},
                                            {QStringLiteral("requestId"), p_requestId}})
                      .toJson(QJsonDocument::Compact);
  response.headers = {{"Cache-Control", "no-store"},
                      {"X-Content-Type-Options", "nosniff"},
                      {"X-Request-Id", p_requestId.toLatin1()}};
  if (p_error.status == 401) {
    response.headers.emplaceBack(QByteArrayLiteral("WWW-Authenticate"),
                                 QByteArrayLiteral("Bearer realm=\"vnote\""));
  }
  if (p_error.status == 429 || p_error.status == 503) {
    response.headers.emplaceBack(QByteArrayLiteral("Retry-After"), QByteArrayLiteral("1"));
  }
  return response;
}

HttpResponse json(const QJsonObject &p_data, const QString &p_requestId, int p_status = 200) {
  HttpResponse response;
  response.status = p_status;
  response.body = QJsonDocument(p_data).toJson(QJsonDocument::Compact);
  const auto etag =
      '"' + QCryptographicHash::hash(response.body, QCryptographicHash::Sha256).toHex() + '"';
  response.headers = {{"Cache-Control", "no-store"},
                      {"X-Content-Type-Options", "nosniff"},
                      {"X-Request-Id", p_requestId.toLatin1()},
                      {"ETag", etag}};
  return response;
}

ApiError invalid(const QString &p_title = QStringLiteral("Invalid request parameters")) {
  return {400, QStringLiteral("invalid_request"), p_title};
}

bool id(const QString &p_value) {
  static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
  return pattern.match(p_value).hasMatch();
}

bool validText(const QString &p_text) {
  for (qsizetype i = 0; i < p_text.size(); ++i) {
    if (p_text[i].isHighSurrogate()) {
      if (++i == p_text.size() || !p_text[i].isLowSurrogate()) {
        return false;
      }
    } else if (p_text[i].isLowSurrogate() || p_text[i].unicode() == 0) {
      return false;
    }
  }
  return true;
}

bool name(const QString &p_name) {
  if (p_name.isEmpty() || p_name.size() > 255 || p_name == QStringLiteral(".") ||
      p_name == QStringLiteral("..") || p_name.endsWith('.') || p_name.endsWith(' ') ||
      !validText(p_name)) {
    return false;
  }
  for (const auto ch : p_name) {
    if (ch.unicode() < 32 || QStringLiteral("/\\:*?\"<>|").contains(ch)) {
      return false;
    }
  }
  // Windows resolves device names even with a file extension. Keep names
  // portable and reject those aliases before any backend filesystem operation.
  const auto dot = p_name.indexOf('.');
  const auto stem = QStringView(p_name).first(dot < 0 ? p_name.size() : dot).trimmed();
  for (const auto reserved : {"CON", "PRN", "AUX", "NUL", "CLOCK$", "CONIN$", "CONOUT$"}) {
    if (stem.compare(QLatin1String(reserved), Qt::CaseInsensitive) == 0) {
      return false;
    }
  }
  if (stem.size() == 4 && (stem.first(3).compare(QLatin1String("COM"), Qt::CaseInsensitive) == 0 ||
                           stem.first(3).compare(QLatin1String("LPT"), Qt::CaseInsensitive) == 0)) {
    const auto digit = stem.back().unicode();
    if ((digit >= '1' && digit <= '9') || digit == 0x00b9 || digit == 0x00b2 || digit == 0x00b3) {
      return false;
    }
  }
  return true;
}

bool decode(const QByteArray &p_raw, QString *p_value) {
  auto hex = [](char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
  };
  for (qsizetype i = 0; i < p_raw.size(); ++i) {
    if (p_raw[i] == '%') {
      if (i + 2 >= p_raw.size() || !hex(p_raw[i + 1]) || !hex(p_raw[i + 2])) {
        return false;
      }
      i += 2;
    }
  }
  QStringDecoder decoder(QStringDecoder::Utf8);
  *p_value = decoder.decode(QByteArray::fromPercentEncoding(p_raw));
  return !decoder.hasError() && validText(*p_value);
}

bool query(const QByteArray &p_raw, QMap<QString, QString> *p_query) {
  if (p_raw.isEmpty()) {
    return true;
  }
  for (const auto &pair : p_raw.split('&')) {
    const auto separator = pair.indexOf('=');
    QString key, value;
    if (separator < 1 || !decode(pair.first(separator), &key) ||
        !decode(pair.sliced(separator + 1), &value) || value.isEmpty() || p_query->contains(key)) {
      return false;
    }
    p_query->insert(key, value);
  }
  return true;
}

// QJsonDocument validates syntax first. This second walk rejects duplicate object members,
// including escaped spellings, and excessive nesting before Qt's last-key-wins view is used.
class UniqueJsonKeys {
public:
  explicit UniqueJsonKeys(const QByteArray &p_bytes) : m_bytes(p_bytes) {}
  bool check() { return value(0); }

private:
  void space() {
    while (m_pos < m_bytes.size() && (m_bytes[m_pos] == ' ' || m_bytes[m_pos] == '\n' ||
                                      m_bytes[m_pos] == '\r' || m_bytes[m_pos] == '\t')) {
      ++m_pos;
    }
  }
  QByteArray string() {
    const auto start = m_pos++;
    while (m_pos < m_bytes.size()) {
      const char ch = m_bytes[m_pos++];
      if (ch == '\\') {
        ++m_pos;
      } else if (ch == '"') {
        break;
      }
    }
    return m_bytes.sliced(start, m_pos - start);
  }
  bool value(int p_depth) {
    if (p_depth > 32) {
      return false;
    }
    space();
    if (m_pos >= m_bytes.size()) {
      return false;
    }
    const char ch = m_bytes[m_pos];
    if (ch == '"') {
      string();
      return true;
    }
    if (ch == '{' || ch == '[') {
      ++m_pos;
      const char close = ch == '{' ? '}' : ']';
      QSet<QString> keys;
      space();
      if (m_bytes[m_pos] == close) {
        ++m_pos;
        return true;
      }
      do {
        space();
        if (ch == '{') {
          const auto key = QJsonDocument::fromJson('[' + string() + ']').array()[0].toString();
          if (keys.contains(key)) {
            return false;
          }
          keys.insert(key);
          space();
          ++m_pos; // colon; syntax was already validated
        }
        if (!value(p_depth + 1)) {
          return false;
        }
        space();
        if (m_bytes[m_pos++] == close) {
          return true;
        }
      } while (m_pos < m_bytes.size());
      return false;
    }
    while (m_pos < m_bytes.size() && m_bytes[m_pos] != ',' && m_bytes[m_pos] != '}' &&
           m_bytes[m_pos] != ']' && m_bytes[m_pos] != ' ' && m_bytes[m_pos] != '\r' &&
           m_bytes[m_pos] != '\n' && m_bytes[m_pos] != '\t') {
      ++m_pos;
    }
    return true;
  }
  const QByteArray &m_bytes;
  qsizetype m_pos = 0;
};

struct Route {
  ApiCommand command;
  enum class Local { None, Info, Operation } local = Local::None;
  QString operationId;
  QByteArray allowed;
  QString auditTemplate;
  bool mutation = false;
  bool body = false;
};

ApiError parseRoute(const HttpRequest &p_request, Route *p_route, bool p_headersOnly) {
  if (!p_request.target.startsWith("/api/v1/") || p_request.target.contains('#') ||
      p_request.target.contains('\\') || p_request.target.size() > 4096) {
    return invalid(QStringLiteral("Invalid request target"));
  }
  const auto question = p_request.target.indexOf('?');
  const auto path = question < 0 ? p_request.target : p_request.target.first(question);
  if (path.contains('%') || path.contains("//") || path.endsWith('/')) {
    return invalid(QStringLiteral("Invalid request path"));
  }
  const auto parts = QString::fromLatin1(path).split('/');
  auto &cmd = p_route->command;
  if (parts.size() == 4 && parts[3] == QStringLiteral("info")) {
    p_route->auditTemplate = QStringLiteral("/api/v1/info");
    p_route->local = Route::Local::Info;
    p_route->allowed = "GET";
  } else if (parts.size() == 4 && parts[3] == QStringLiteral("notebooks")) {
    p_route->auditTemplate = QStringLiteral("/api/v1/notebooks");
    cmd.kind = ApiCommandKind::ListNotebooks;
    p_route->allowed = "GET";
  } else if (parts.size() == 4 && parts[3] == QStringLiteral("search")) {
    p_route->auditTemplate = QStringLiteral("/api/v1/search");
    cmd.kind = ApiCommandKind::Search;
    p_route->allowed = "POST";
    p_route->body = true;
  } else if (parts.size() == 5 && parts[3] == QStringLiteral("operations")) {
    static const QRegularExpression operation(QStringLiteral("^[0-9a-f]{32}\\.[0-9a-f]{32}$"));
    if (!operation.match(parts[4]).hasMatch()) {
      return invalid();
    }
    p_route->auditTemplate = QStringLiteral("/api/v1/operations/{operationId}");
    p_route->local = Route::Local::Operation;
    p_route->operationId = parts[4];
    p_route->allowed = "GET";
  } else if (parts.size() >= 6 && parts[3] == QStringLiteral("notebooks") && id(parts[4])) {
    cmd.notebookId = parts[4];
    if (parts.size() == 6 && parts[5] == QStringLiteral("nodes")) {
      p_route->auditTemplate = QStringLiteral("/api/v1/notebooks/{notebookId}/nodes");
      cmd.kind = ApiCommandKind::ListNodes;
      p_route->allowed = "GET";
    } else if (parts.size() == 6 && parts[5] == QStringLiteral("notes")) {
      p_route->auditTemplate = QStringLiteral("/api/v1/notebooks/{notebookId}/notes");
      cmd.kind = ApiCommandKind::CreateNote;
      p_route->allowed = "POST";
      p_route->mutation = p_route->body = true;
    } else if (parts.size() >= 7 && parts[5] == QStringLiteral("notes") && id(parts[6])) {
      cmd.nodeId = parts[6];
      if (parts.size() == 7) {
        p_route->auditTemplate = QStringLiteral("/api/v1/notebooks/{notebookId}/notes/{noteId}");
        p_route->allowed = "GET, PATCH, DELETE";
        cmd.kind = p_request.method == "PATCH"    ? ApiCommandKind::RenameNote
                   : p_request.method == "DELETE" ? ApiCommandKind::TrashNote
                                                  : ApiCommandKind::ReadNote;
        p_route->body = p_request.method == "PATCH";
        p_route->mutation = p_request.method != "GET";
      } else if (parts.size() == 8 && parts[7] == QStringLiteral("content")) {
        p_route->auditTemplate =
            QStringLiteral("/api/v1/notebooks/{notebookId}/notes/{noteId}/content");
        p_route->allowed = "GET, PUT";
        cmd.kind = p_request.method == "PUT" ? ApiCommandKind::ReplaceContent
                                             : ApiCommandKind::ReadContent;
        p_route->mutation = p_route->body = p_request.method == "PUT";
        if (!p_route->mutation) {
          cmd.limit = 65536;
        }
      } else if (parts.size() == 8 && parts[7] == QStringLiteral("append")) {
        p_route->auditTemplate =
            QStringLiteral("/api/v1/notebooks/{notebookId}/notes/{noteId}/append");
        p_route->allowed = "POST";
        cmd.kind = ApiCommandKind::AppendContent;
        p_route->mutation = p_route->body = true;
      }
    }
  }
  if (p_route->allowed.isEmpty()) {
    return {404, QStringLiteral("not_found"), QStringLiteral("Resource not found")};
  }
  if (!p_route->allowed.split(',').contains(p_request.method) &&
      !p_route->allowed.split(',').contains(' ' + p_request.method)) {
    return {405, QStringLiteral("method_not_allowed"), QStringLiteral("Method not allowed")};
  }
  QMap<QString, QString> values;
  if (!query(question < 0 ? QByteArray() : p_request.target.sliced(question + 1), &values)) {
    return invalid();
  }
  QSet<QString> allowedQuery;
  if (cmd.kind == ApiCommandKind::ListNodes && p_route->local == Route::Local::None) {
    allowedQuery = {QStringLiteral("parentId"), QStringLiteral("limit"), QStringLiteral("cursor")};
  } else if (cmd.kind == ApiCommandKind::ReadContent) {
    allowedQuery = {QStringLiteral("view"), QStringLiteral("offset"), QStringLiteral("limit"),
                    QStringLiteral("cursor")};
  }
  for (auto it = values.cbegin(); it != values.cend(); ++it) {
    if (!allowedQuery.contains(it.key())) {
      return invalid();
    }
  }
  if (values.contains(QStringLiteral("parentId"))) {
    cmd.parentId = values.value(QStringLiteral("parentId"));
    if (!id(cmd.parentId)) {
      return invalid();
    }
  }
  cmd.cursor = values.value(QStringLiteral("cursor"));
  if (cmd.cursor.size() > 2048) {
    return invalid();
  }
  if (values.contains(QStringLiteral("view"))) {
    const auto view = values.value(QStringLiteral("view"));
    if (view != QStringLiteral("saved") && view != QStringLiteral("live")) {
      return invalid();
    }
    cmd.view = view == QStringLiteral("live") ? ApiContentView::Live : ApiContentView::Saved;
  }
  static const QRegularExpression integer(QStringLiteral("^(0|[1-9][0-9]{0,15})$"));
  for (const auto &key : {QStringLiteral("offset"), QStringLiteral("limit")}) {
    if (values.contains(key)) {
      bool ok;
      const auto number = values.value(key).toLongLong(&ok);
      if (!ok || !integer.match(values.value(key)).hasMatch() || number < 0) {
        return invalid();
      }
      if (key == QStringLiteral("offset")) {
        cmd.offset = number;
      } else {
        const int maximum = cmd.kind == ApiCommandKind::ReadContent ? 1048576 : 200;
        if (number < 1 || number > maximum) {
          return invalid();
        }
        cmd.limit = static_cast<int>(number);
      }
    }
  }
  if (cmd.offset > 0 && cmd.cursor.isEmpty()) {
    return invalid(QStringLiteral("A content continuation requires its snapshot cursor"));
  }
  if (p_request.headerCount("If-Match") > 1) {
    return invalid(QStringLiteral("Supply exactly one strong If-Match validator"));
  }
  if (p_route->mutation && cmd.kind != ApiCommandKind::CreateNote) {
    if (!p_request.headerCount("If-Match")) {
      return {428, QStringLiteral("precondition_required"),
              QStringLiteral("A full-resource If-Match validator is required")};
    }
    const auto version = p_request.header("If-Match").trimmed();
    if (version.size() < 3 || version.size() > 256 || !version.startsWith('"') ||
        !version.endsWith('"')) {
      return invalid(QStringLiteral("Supply exactly one strong If-Match validator"));
    }
    for (qsizetype i = 1; i < version.size() - 1; ++i) {
      if (version[i] <= 32 || version[i] >= 127 || version[i] == '"' || version[i] == ',') {
        return invalid(QStringLiteral("Supply exactly one strong If-Match validator"));
      }
    }
    cmd.expectedVersion = QString::fromLatin1(version.sliced(1, version.size() - 2));
  } else if (p_request.headerCount("If-Match")) {
    return invalid(QStringLiteral("If-Match is not accepted on this route"));
  }
  if (p_request.headerCount("Content-Encoding")) {
    return {415, QStringLiteral("unsupported_content_encoding"),
            QStringLiteral("Content encodings are not supported")};
  }
  if (p_route->body) {
    if (p_request.headerCount("Content-Type") != 1) {
      return {415, QStringLiteral("unsupported_media_type"),
              QStringLiteral("Use application/json encoded as UTF-8")};
    }
    static const QRegularExpression jsonType(QStringLiteral(
        "^application/json(?:[ \\t]*;[ \\t]*charset[ \\t]*=[ \\t]*(?:utf-8|\"utf-8\"))?$"));
    if (!jsonType.match(QString::fromLatin1(p_request.header("Content-Type").trimmed().toLower()))
             .hasMatch()) {
      return {415, QStringLiteral("unsupported_media_type"),
              QStringLiteral("Use application/json encoded as UTF-8")};
    }
  } else if (p_request.headerCount("Transfer-Encoding") ||
             (!p_request.header("Content-Length").isEmpty() &&
              p_request.header("Content-Length") != "0") ||
             !p_request.body.isEmpty()) {
    return invalid(QStringLiteral("This route does not accept a body"));
  }
  if (p_headersOnly || !p_route->body) {
    return {};
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(p_request.body, &parseError);
  if (!p_request.body.isValidUtf8() || parseError.error != QJsonParseError::NoError ||
      !document.isObject() || !UniqueJsonKeys(p_request.body).check()) {
    return invalid(QStringLiteral("The body must be a unique-key UTF-8 JSON object"));
  }
  const auto object = document.object();
  QSet<QString> allowedBody;
  switch (cmd.kind) {
  case ApiCommandKind::CreateNote:
    allowedBody = {QStringLiteral("name"), QStringLiteral("format"), QStringLiteral("parentId")};
    cmd.name = object.value(QStringLiteral("name")).toString();
    cmd.format = object.value(QStringLiteral("format")).toString();
    if (!name(cmd.name) ||
        (cmd.format != QStringLiteral("markdown") && cmd.format != QStringLiteral("text"))) {
      return invalid();
    }
    if (object.contains(QStringLiteral("parentId"))) {
      cmd.parentId = object.value(QStringLiteral("parentId")).toString();
      if (!id(cmd.parentId)) {
        return invalid();
      }
    }
    break;
  case ApiCommandKind::RenameNote:
    allowedBody = {QStringLiteral("name")};
    cmd.name = object.value(QStringLiteral("name")).toString();
    if (!name(cmd.name)) {
      return invalid();
    }
    break;
  case ApiCommandKind::ReplaceContent:
  case ApiCommandKind::AppendContent:
    allowedBody = {QStringLiteral("text")};
    if (!object.value(QStringLiteral("text")).isString()) {
      return invalid();
    }
    cmd.text = object.value(QStringLiteral("text")).toString();
    if (!validText(cmd.text)) {
      return invalid();
    }
    break;
  case ApiCommandKind::Search:
    allowedBody = {QStringLiteral("query"), QStringLiteral("notebookIds"),
                   QStringLiteral("searchContent"), QStringLiteral("limit")};
    cmd.query = object.value(QStringLiteral("query")).toString();
    if (cmd.query.isEmpty() || cmd.query.size() > 1024 || !validText(cmd.query)) {
      return invalid();
    }
    if (object.contains(QStringLiteral("searchContent"))) {
      if (!object.value(QStringLiteral("searchContent")).isBool()) {
        return invalid();
      }
      cmd.searchContent = object.value(QStringLiteral("searchContent")).toBool();
    }
    if (object.contains(QStringLiteral("limit"))) {
      const auto value = object.value(QStringLiteral("limit"));
      const auto number = value.toDouble();
      if (!value.isDouble() || number < 1 || number > 50 || std::floor(number) != number) {
        return invalid();
      }
      cmd.limit = static_cast<int>(number);
    }
    if (object.contains(QStringLiteral("notebookIds"))) {
      const auto array = object.value(QStringLiteral("notebookIds"));
      if (!array.isArray() || array.toArray().isEmpty() || array.toArray().size() > 256) {
        return invalid();
      }
      for (const auto &item : array.toArray()) {
        if (!item.isString() || !id(item.toString()) || cmd.notebookIds.contains(item.toString())) {
          return invalid();
        }
        cmd.notebookIds.append(item.toString());
      }
    }
    break;
  default:
    break;
  }
  for (auto it = object.begin(); it != object.end(); ++it) {
    if (!allowedBody.contains(it.key())) {
      return invalid(QStringLiteral("Unknown JSON member"));
    }
  }
  return {};
}

struct Waiter {
  std::mutex mutex;
  std::condition_variable ready;
  bool done = false;
  ApiResult result;
  std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
  void finish(ApiResult p_result) {
    std::lock_guard lock(mutex);
    if (!done) {
      result = std::move(p_result);
      done = true;
      ready.notify_all();
    }
  }
};

struct Shared {
  explicit Shared(QString p_instance) : operations(std::move(p_instance)) {}
  std::atomic_bool stopping{false};
  std::mutex mutex;
  QHash<QString, std::shared_ptr<Waiter>> waiters;
  int searches = 0;
  ApiOperations operations;
};
} // namespace

struct ApiServer::Impl {
  ApiServer *owner;
  IApiBackend &backend;
  ApiAuth &auth;
  HttpServer http;
  QString instance = QUuid::createUuid().toString(QUuid::Id128);
  std::shared_ptr<Shared> shared = std::make_shared<Shared>(instance);
  QByteArray host;
  qsizetype maxBodyBytes = 1048576;
  bool started = false;

  Impl(ApiServer *p_owner, IApiBackend &p_backend, ApiAuth &p_auth)
      : owner(p_owner), backend(p_backend), auth(p_auth) {}

  std::optional<ApiPrincipal> principal(const HttpRequest &p_request) const {
    if (p_request.headerCount("Authorization") != 1) {
      return std::nullopt;
    }
    const auto authorization = p_request.header("Authorization");
    if (authorization.size() != 83 || authorization.first(7).toLower() != "bearer ") {
      return std::nullopt;
    }
    return auth.authenticate(authorization.sliced(7));
  }

  std::optional<HttpResponse> admit(HttpRequest &p_request) const {
    const auto &requestId = p_request.audit.requestId;
    if (shared->stopping.load()) {
      return problem({503, QStringLiteral("stopping"), QStringLiteral("The API is stopping")},
                     requestId);
    }
    if (p_request.headerCount("Host") != 1 || p_request.header("Host") != host) {
      return problem({400, QStringLiteral("invalid_host"), QStringLiteral("Invalid Host header")},
                     requestId);
    }
    if (p_request.headerCount("Origin")) {
      return problem({403, QStringLiteral("browser_origin_rejected"),
                      QStringLiteral("Browser Origin requests are not supported")},
                     requestId);
    }
    if (p_request.headerCount("Cookie")) {
      return problem({400, QStringLiteral("cookies_not_supported"),
                      QStringLiteral("Cookies are not accepted")},
                     requestId);
    }
    const auto identity = principal(p_request);
    if (!identity) {
      return problem({401, QStringLiteral("invalid_token"),
                      QStringLiteral("A valid bearer token is required")},
                     requestId);
    }
    p_request.audit.principalId = identity->tokenId;
    Route route;
    const auto error = parseRoute(p_request, &route, true);
    p_request.audit.routeTemplate = route.auditTemplate;
    if (error.status) {
      auto response = problem(error, requestId);
      if (error.status == 405) {
        response.headers.emplaceBack(QByteArrayLiteral("Allow"), route.allowed);
      }
      return response;
    }
    const auto policy = authorize(*identity, route);
    return policy.status ? std::optional<HttpResponse>(problem(policy, requestId)) : std::nullopt;
  }

  ApiError authorize(const ApiPrincipal &p_principal, const Route &p_route) const {
    if (p_route.local != Route::Local::None) {
      return {};
    }
    if ((!p_route.command.notebookId.isEmpty() &&
         !p_principal.notebookIds.contains(p_route.command.notebookId))) {
      return {404, QStringLiteral("not_found"), QStringLiteral("Resource not found")};
    }
    for (const auto &notebook : p_route.command.notebookIds) {
      if (!p_principal.notebookIds.contains(notebook)) {
        return {404, QStringLiteral("not_found"), QStringLiteral("Resource not found")};
      }
    }
    if (!auth.isAuthorized(p_principal, p_route.command)) {
      return {403, QStringLiteral("insufficient_scope"),
              QStringLiteral("The token does not grant this action")};
    }
    return {};
  }

  HttpResponse handle(HttpRequest &p_request) {
    if (auto denial = admit(p_request)) {
      return std::move(*denial);
    }
    const auto &requestId = p_request.audit.requestId;
    const auto identity = principal(p_request);
    if (!identity) {
      return problem({401, QStringLiteral("invalid_token"),
                      QStringLiteral("A valid bearer token is required")},
                     requestId);
    }
    Route route;
    const auto error = parseRoute(p_request, &route, false);
    p_request.audit.routeTemplate = route.auditTemplate;
    if (error.status) {
      return problem(error, requestId);
    }
    const auto policy = authorize(*identity, route);
    if (policy.status) {
      return problem(policy, requestId);
    }
    if (route.local == Route::Local::Info) {
      return json(
          {{QStringLiteral("apiVersion"), QStringLiteral("1")},
           {QStringLiteral("instanceId"), instance},
           {QStringLiteral("capabilities"),
            QJsonArray{"savedContent", "liveContent", "search", "create", "replace", "append",
                       "rename", "trash"}},
           {QStringLiteral("limits"),
            QJsonObject{{QStringLiteral("requestBodyBytes"), static_cast<qint64>(maxBodyBytes)},
                        {QStringLiteral("listItems"), 200},
                        {QStringLiteral("searchItems"), 50},
                        {QStringLiteral("contentBytes"), 1048576},
                        {QStringLiteral("backendDeadlineSeconds"), c_deadlineSeconds},
                        {QStringLiteral("activeOperations"), 16},
                        {QStringLiteral("activeOperationsPerToken"), 4},
                        {QStringLiteral("retainedOperations"), 256},
                        {QStringLiteral("operationRetentionSeconds"), 900},
                        {QStringLiteral("activeAndQueuedSearches"), 3}}}},
          requestId);
    }
    if (route.local == Route::Local::Operation) {
      const auto result = shared->operations.lookup(route.operationId, identity->tokenId);
      return result.error.status ? problem(result.error, requestId) : json(result.data, requestId);
    }
    ApiRequestContext context;
    context.requestId = requestId;
    context.principal = *identity;
    const auto grant = auth.authorizer(*identity, route.command);
    std::weak_ptr<Shared> weak = shared;
    context.authorized = [weak, grant]() {
      const auto state = weak.lock();
      return state && !state->stopping.load() && grant();
    };
    context.cancelled = std::make_shared<std::atomic_bool>(false);
    if (route.mutation) {
      ApiError admissionError;
      const auto operation = shared->operations.admit(p_request.audit, context.authorized,
                                                      context.cancelled, &admissionError);
      if (admissionError.status) {
        return problem(admissionError, requestId);
      }
      auto state = shared;
      auto *target = &backend;
      const bool queued = QMetaObject::invokeMethod(
          owner,
          [state, target, operation, command = std::move(route.command), context]() mutable {
            if (state->stopping.load() || !state->operations.running(operation)) {
              return;
            }
            target->execute(std::move(command), std::move(context),
                            [state, operation](ApiResult result) {
                              state->operations.complete(operation, std::move(result));
                            });
          },
          Qt::QueuedConnection);
      if (!queued) {
        context.cancelled->store(true);
        shared->operations.complete(operation, {{},
                                                {503, QStringLiteral("unavailable"),
                                                 QStringLiteral("The backend is unavailable")}});
        return problem(
            {503, QStringLiteral("unavailable"), QStringLiteral("The backend is unavailable")},
            requestId);
      }
      auto response = json({{QStringLiteral("id"), operation},
                            {QStringLiteral("state"), QStringLiteral("pending")},
                            {QStringLiteral("committed"), false},
                            {QStringLiteral("saved"), false}},
                           requestId, 202);
      response.headers.emplaceBack(QByteArrayLiteral("Location"),
                                   QByteArrayLiteral("/api/v1/operations/") + operation.toLatin1());
      return response;
    }
    auto waiter = std::make_shared<Waiter>();
    context.cancelled = waiter->cancelled;
    const bool search = route.command.kind == ApiCommandKind::Search;
    {
      std::lock_guard lock(shared->mutex);
      if (shared->stopping.load()) {
        return problem({503, QStringLiteral("stopping"), QStringLiteral("The API is stopping")},
                       requestId);
      }
      if (search && shared->searches >= 3) {
        return problem(
            {429, QStringLiteral("search_limit"), QStringLiteral("Too many queued searches")},
            requestId);
      }
      shared->searches += search;
      shared->waiters.insert(requestId, waiter);
    }
    auto state = shared;
    auto *target = &backend;
    const bool queued = QMetaObject::invokeMethod(
        owner,
        [state, target, waiter, command = std::move(route.command), context]() mutable {
          if (state->stopping.load() || context.cancelled->load() || !context.authorized()) {
            waiter->finish({{},
                            {401, QStringLiteral("authorization_revoked"),
                             QStringLiteral("The request is no longer authorized")}});
            return;
          }
          target->execute(std::move(command), std::move(context),
                          [waiter](ApiResult result) { waiter->finish(std::move(result)); });
        },
        Qt::QueuedConnection);
    if (!queued) {
      waiter->finish(
          {{}, {503, QStringLiteral("unavailable"), QStringLiteral("The backend is unavailable")}});
    }
    ApiResult result;
    {
      std::unique_lock lock(waiter->mutex);
      if (!waiter->ready.wait_for(lock, std::chrono::seconds(c_deadlineSeconds),
                                  [&] { return waiter->done; })) {
        waiter->cancelled->store(true);
        waiter->done = true;
        waiter->result = {{},
                          {503, QStringLiteral("deadline_exceeded"),
                           QStringLiteral("The backend request deadline was exceeded")}};
      }
      result = std::move(waiter->result);
    }
    {
      std::lock_guard lock(shared->mutex);
      shared->waiters.remove(requestId);
      shared->searches -= search;
    }
    if (shared->stopping.load()) {
      return problem({503, QStringLiteral("stopping"), QStringLiteral("The API is stopping")},
                     requestId);
    }
    if (!context.authorized()) {
      return problem({401, QStringLiteral("authorization_revoked"),
                      QStringLiteral("The request is no longer authorized")},
                     requestId);
    }
    return result.error.status ? problem(result.error, requestId) : json(result.data, requestId);
  }
};

ApiServer::ApiServer(IApiBackend &p_backend, ApiAuth &p_auth, QObject *p_parent)
    : QObject(p_parent), m_impl(std::make_unique<Impl>(this, p_backend, p_auth)) {}

ApiServer::~ApiServer() { stop(); }

bool ApiServer::start(const HttpServerOptions &p_options, QString *p_error) {
  if (QThread::currentThread() != thread() || m_impl->started || m_impl->shared->stopping.load()) {
    if (p_error) {
      *p_error = QStringLiteral("Create a new API server and backend before restarting");
    }
    return false;
  }
  if ((p_options.address != QStringLiteral("127.0.0.1") &&
       p_options.address != QStringLiteral("::1")) ||
      p_options.port == 0 || p_options.maxBodyBytes < 1 || p_options.maxBodyBytes > 1048576) {
    if (p_error) {
      *p_error = QStringLiteral("Use an explicit loopback address, port, and bounded request size");
    }
    return false;
  }
  m_impl->host = (p_options.address == QStringLiteral("::1") ? QByteArray("[::1]")
                                                             : p_options.address.toLatin1()) +
                 ':' + QByteArray::number(p_options.port);
  m_impl->maxBodyBytes = p_options.maxBodyBytes;
  auto options = p_options;
  options.accessObserver = logApiAccess;
  const bool result = m_impl->http.start(
      options, [impl = m_impl.get()](HttpRequest &request) { return impl->admit(request); },
      [impl = m_impl.get()](HttpRequest &request) { return impl->handle(request); }, p_error);
  m_impl->started = result;
  return result;
}

void ApiServer::stop() {
  Q_ASSERT(QThread::currentThread() == thread());
  auto state = m_impl->shared;
  if (state->stopping.exchange(true)) {
    return;
  }
  state->operations.stop();
  {
    std::lock_guard lock(state->mutex);
    for (const auto &waiter : state->waiters) {
      waiter->cancelled->store(true);
      waiter->finish(
          {{}, {503, QStringLiteral("stopping"), QStringLiteral("The API is stopping")}});
    }
    state->waiters.clear();
  }
  // All waiters are settled before stop can synchronously complete backend commands or join
  // sockets.
  m_impl->backend.stop();
  m_impl->http.stop();
}

bool ApiServer::isListening() const { return m_impl->http.isListening(); }
QString ApiServer::instanceId() const { return m_impl->instance; }
