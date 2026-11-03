#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QtTest>
#include <algorithm>
#include <api/apiauth.h>
#include <api/apioperations.h>
#include <api/apiserver.h>
#include <api/iapibackend.h>
#include <future>
#include <mutex>
#include <vector>

using namespace vnotex;

namespace {

class AuditCapture {
public:
  AuditCapture() {
    std::lock_guard lock(mutex);
    active = this;
    previous = qInstallMessageHandler(handler);
  }
  ~AuditCapture() {
    std::lock_guard lock(mutex);
    qInstallMessageHandler(previous);
    active = nullptr;
  }
  QString text() const {
    std::lock_guard lock(mutex);
    return messages.join('\n');
  }
  QList<QJsonObject> records(const QString &p_event) const {
    std::lock_guard lock(mutex);
    QList<QJsonObject> result;
    for (const auto &message : messages) {
      const auto object = QJsonDocument::fromJson(message.toUtf8()).object();
      if (object.value("event").toString() == p_event) {
        result.append(object);
      }
    }
    return result;
  }

private:
  static void handler(QtMsgType p_type, const QMessageLogContext &p_context,
                      const QString &p_message) {
    QtMessageHandler forward;
    {
      std::lock_guard lock(mutex);
      if (active && p_type == QtInfoMsg && QByteArray(p_context.category) == "vnote.api.audit") {
        active->messages.append(p_message);
        return;
      }
      forward = previous;
    }
    if (forward) {
      forward(p_type, p_context, p_message);
    }
  }
  inline static std::mutex mutex;
  inline static AuditCapture *active = nullptr;
  inline static QtMessageHandler previous = nullptr;
  QStringList messages;
};

class MemoryStore : public IApiCredentialStore {
public:
  QJsonArray records;
  bool load(QJsonArray *p_records, QString *) override {
    *p_records = records;
    return true;
  }
  bool save(const QJsonArray &p_records, QString *) override {
    records = p_records;
    return true;
  }
};

QString version(const char *p_kind, const QString &p_value) {
  return QString::fromLatin1(p_kind) + '-' +
         QString::fromLatin1(
             QCryptographicHash::hash(p_value.toUtf8(), QCryptographicHash::Sha256).toHex());
}

struct Note {
  QString id, notebook, name, text;
  QString live;
  bool dirty = false;
  bool protectedNote = false;
  bool readable = true;
  QString metadataVersion() const { return version("meta", id + name); }
  QString contentVersion() const { return version("saved", id + text); }
  QString stateVersion() const { return version("state", metadataVersion() + contentVersion()); }
  QJsonObject metadata() const {
    return {{"id", id},
            {"notebookId", notebook},
            {"name", name},
            {"path", name},
            {"metadataVersion", metadataVersion()},
            {"type", "file"},
            {"contentReadable", readable}};
  }
};

// A real in-memory model: stable identities, persistent/live representations, optimistic
// conflicts, name collisions, byte-boundary paging and reversible trash. No command echoes.
class MemoryBackend : public IApiBackend {
public:
  QHash<QString, Note> notes;
  QHash<QString, Note> trash;
  QThread *owner = QThread::currentThread();
  bool stopped = false;
  bool hold = false;
  bool failSave = false;
  bool failAfterSave = false;
  int sequence = 0;
  int calls = 0;
  struct Pending {
    ApiCommand command;
    ApiRequestContext context;
    Completion completion;
  };
  QList<Pending> pending;

  MemoryBackend() {
    notes.insert("note-a", {"note-a", "nb-a", "hello.md", QString::fromUtf8("hello é world")});
    notes.insert("note-b", {"note-b", "nb-b", "secret.md", "private words"});
    Note protectedNote{"protected", "nb-a", "classified.md", "hidden words"};
    protectedNote.protectedNote = true;
    notes.insert(protectedNote.id, protectedNote);
    Note binary{"binary", "nb-a", "photo.png", "not text"};
    binary.readable = false;
    notes.insert(binary.id, binary);
  }

  void execute(ApiCommand p_command, ApiRequestContext p_context, Completion p_complete) override {
    Q_ASSERT(QThread::currentThread() == owner);
    ++calls;
    if (hold) {
      pending.append({std::move(p_command), std::move(p_context), std::move(p_complete)});
    } else {
      p_complete(run(p_command, p_context));
    }
  }

  void release() {
    auto work = std::move(pending);
    pending.clear();
    for (auto &entry : work) {
      entry.completion(run(entry.command, entry.context));
    }
  }

  void stop() override {
    Q_ASSERT(QThread::currentThread() == owner);
    stopped = true;
    // Deliberately retain completions to prove API waiter detachment, independent of backend
    // cleanup.
  }

  ApiResult run(const ApiCommand &cmd, const ApiRequestContext &ctx) {
    if (stopped || ctx.cancelled->load() || !ctx.authorized()) {
      return {{}, {503, "cancelled", "Request cancelled"}};
    }
    if (cmd.kind == ApiCommandKind::ListNotebooks) {
      QJsonArray items;
      for (const auto &nb : {QStringLiteral("nb-a"), QStringLiteral("nb-b")}) {
        if (ctx.principal.notebookIds.contains(nb)) {
          items.append(QJsonObject{{"id", nb},
                                   {"name", nb},
                                   {"readOnly", false},
                                   {"capabilities", QJsonObject{{"trash", true}}}});
        }
      }
      return {{{"items", items}}};
    }
    if (cmd.kind == ApiCommandKind::ListNodes || cmd.kind == ApiCommandKind::Search) {
      QJsonArray items;
      auto keys = notes.keys();
      keys.sort();
      for (const auto &key : keys) {
        const auto &note = notes.constFind(key).value();
        if (note.protectedNote || !ctx.principal.notebookIds.contains(note.notebook) ||
            (!cmd.notebookId.isEmpty() && note.notebook != cmd.notebookId) ||
            (!cmd.notebookIds.isEmpty() && !cmd.notebookIds.contains(note.notebook))) {
          continue;
        }
        if (cmd.kind == ApiCommandKind::Search &&
            (!note.readable || !(cmd.searchContent ? note.text : note.name).contains(cmd.query))) {
          continue;
        }
        auto item = note.metadata();
        if (cmd.kind == ApiCommandKind::Search) {
          item.insert("snippet", note.text);
        }
        items.append(item);
        if (items.size() == cmd.limit) {
          break;
        }
      }
      return {{{"items", items}, {"nextCursor", QJsonValue::Null}, {"truncated", false}}};
    }
    if (cmd.kind == ApiCommandKind::CreateNote) {
      for (const auto &note : notes) {
        if (note.notebook == cmd.notebookId && note.name == cmd.name) {
          return {{}, {409, "name_conflict", "The name already exists"}};
        }
      }
      Note created{QStringLiteral("created-%1").arg(++sequence), cmd.notebookId, cmd.name, {}};
      const auto result = created.metadata();
      const auto createdId = created.id;
      notes.insert(createdId, std::move(created));
      return {result, {}, true, true};
    }
    auto it = notes.find(cmd.nodeId);
    if (it == notes.end() || it->notebook != cmd.notebookId) {
      return {{}, {404, "not_found", "Resource not found"}};
    }
    auto &note = *it;
    if (note.protectedNote) {
      return {{}, {422, "protected_note_unavailable", "Protected notes are unavailable"}};
    }
    if (cmd.kind == ApiCommandKind::ReadNote) {
      return {note.metadata()};
    }
    if (!note.readable) {
      return {{}, {422, "unsupported_note_type", "This note is not ordinary text"}};
    }
    if (cmd.kind == ApiCommandKind::ReadContent) {
      const bool live = cmd.view == ApiContentView::Live && note.dirty;
      const auto text = live ? note.live : note.text;
      const auto bytes = text.toUtf8();
      const auto validator = live ? version("live", note.id + text) : note.contentVersion();
      const auto cursor = validator + ':' + QString::number(cmd.offset);
      if (!cmd.cursor.isEmpty() && cmd.cursor != cursor) {
        return {{}, {412, "snapshot_changed", "The content snapshot changed"}};
      }
      if (cmd.offset > bytes.size() ||
          (cmd.offset < bytes.size() &&
           (static_cast<unsigned char>(bytes[cmd.offset]) & 0xc0) == 0x80)) {
        return {{}, {400, "invalid_offset", "Offset is not a UTF-8 boundary"}};
      }
      auto end = std::min<qint64>(bytes.size(), cmd.offset + cmd.limit);
      while (end > cmd.offset && end < bytes.size() &&
             (static_cast<unsigned char>(bytes[end]) & 0xc0) == 0x80) {
        --end;
      }
      if (end == cmd.offset && end < bytes.size()) {
        return {{}, {400, "limit_too_small", "Limit cannot contain the next UTF-8 character"}};
      }
      QJsonObject data{{"id", note.id},
                       {"notebookId", note.notebook},
                       {"view", live ? "live" : "saved"},
                       {"text", QString::fromUtf8(bytes.sliced(cmd.offset, end - cmd.offset))},
                       {"encoding", "UTF-8"},
                       {"lineEnding", "LF"},
                       {"dirty", note.dirty},
                       {"contentVersion", validator},
                       {"offset", cmd.offset},
                       {"returnedBytes", end - cmd.offset},
                       {"totalBytes", bytes.size()},
                       {"nextCursor", end < bytes.size()
                                          ? QJsonValue(validator + ':' + QString::number(end))
                                          : QJsonValue::Null}};
      if (!live) {
        data.insert("noteStateVersion", note.stateVersion());
      }
      return {data};
    }
    const auto expected = cmd.kind == ApiCommandKind::RenameNote  ? note.metadataVersion()
                          : cmd.kind == ApiCommandKind::TrashNote ? note.stateVersion()
                                                                  : note.contentVersion();
    if (cmd.expectedVersion != expected) {
      return {{}, {412, "precondition_failed", "The note changed"}};
    }
    if (note.dirty) {
      return {{}, {409, "unsaved_changes", "The note has unsaved changes"}};
    }
    if (cmd.kind == ApiCommandKind::RenameNote) {
      for (const auto &other : notes) {
        if (other.id != note.id && other.notebook == note.notebook && other.name == cmd.name) {
          return {{}, {409, "name_conflict", "The name already exists"}};
        }
      }
      note.name = cmd.name;
      return {note.metadata(), {}, true, true};
    }
    if (cmd.kind == ApiCommandKind::TrashNote) {
      const auto metadata = note.metadata();
      trash.insert(note.id, note);
      notes.erase(it);
      return {metadata, {}, true, true};
    }
    const auto replacement =
        cmd.kind == ApiCommandKind::AppendContent ? note.text + cmd.text : cmd.text;
    if (failSave) {
      failSave = false;
      note.live = replacement;
      note.dirty = true;
      return {note.metadata(),
              {503, "save_failed", "The changed buffer could not be saved"},
              true,
              false};
    }
    note.text = replacement;
    auto data = note.metadata();
    data.insert("contentVersion", note.contentVersion());
    data.insert("noteStateVersion", note.stateVersion());
    if (failAfterSave) {
      failAfterSave = false;
      return {data, {500, "after_hook_failed", "A post-save hook failed"}, true, true};
    }
    return {data, {}, true, true};
  }
};

struct Response {
  int status = 0;
  int requestIdHeaders = 0;
  QMap<QByteArray, QByteArray> headers;
  QByteArray bytes;
  QJsonObject object() const { return QJsonDocument::fromJson(bytes).object(); }
};

Response exchange(quint16 p_port, const QByteArray &p_request) {
  QTcpSocket socket;
  socket.connectToHost(QHostAddress::LocalHost, p_port);
  if (!socket.waitForConnected(3000)) {
    return {};
  }
  socket.write(p_request);
  if (!socket.waitForBytesWritten(3000)) {
    return {};
  }
  QByteArray bytes;
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 30000) {
    bytes += socket.readAll();
    if (socket.state() == QAbstractSocket::UnconnectedState) {
      break;
    }
    socket.waitForReadyRead(100);
  }
  bytes += socket.readAll();
  const auto separator = bytes.indexOf("\r\n\r\n");
  if (separator < 0) {
    return {};
  }
  Response response;
  const auto lines = bytes.first(separator).split('\n');
  response.status = lines[0].split(' ').value(1).toInt();
  for (qsizetype i = 1; i < lines.size(); ++i) {
    const auto colon = lines[i].indexOf(':');
    if (colon > 0) {
      response.requestIdHeaders += lines[i].first(colon).toLower() == "x-request-id";
      response.headers.insert(lines[i].first(colon).toLower(),
                              lines[i].sliced(colon + 1).trimmed());
    }
  }
  response.bytes = bytes.sliced(separator + 4);
  return response;
}
} // namespace

class TestRestApi : public QObject {
  Q_OBJECT
private:
  MemoryStore store;
  std::unique_ptr<ApiAuth> auth;
  std::unique_ptr<MemoryBackend> backend;
  std::unique_ptr<ApiServer> server;
  ApiIssuedToken token, reader, other;
  quint16 port = 0;

  QByteArray wire(const QByteArray &method, const QByteArray &path, const QByteArray &body = {},
                  const QByteArray &extra = {}, const QString &secret = {}) const {
    return method + ' ' + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) +
           "\r\nAuthorization: Bearer " + (secret.isEmpty() ? token.secret : secret).toLatin1() +
           "\r\nConnection: close\r\n" +
           (body.isEmpty() ? QByteArray()
                           : "Content-Type: application/json\r\nContent-Length: " +
                                 QByteArray::number(body.size()) + "\r\n") +
           extra + "\r\n" + body;
  }

  Response wait(std::future<Response> &future) {
    QElapsedTimer timer;
    timer.start();
    while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           timer.elapsed() < 35000) {
      QCoreApplication::processEvents();
      QTest::qWait(1);
    }
    return future.get();
  }

  Response request(const QByteArray &method, const QByteArray &path, const QByteArray &body = {},
                   const QByteArray &extra = {}, const QString &secret = {}) {
    const auto raw = wire(method, path, body, extra, secret);
    auto future = std::async(std::launch::async, exchange, port, raw);
    return wait(future);
  }

  Response operation(const Response &admission) {
    QElapsedTimer timer;
    timer.start();
    Response response;
    do {
      response = request("GET", admission.headers.value("location"));
      const auto state = response.object()["state"].toString();
      if (response.status != 200 || (state != "pending" && state != "running")) {
        break;
      }
      QTest::qWait(1);
    } while (timer.elapsed() < 5000);
    return response;
  }

  QByteArray match(const QString &value) const {
    return "If-Match: \"" + value.toLatin1() + "\"\r\n";
  }

private slots:
  void init() {
    store.records = {};
    auth = std::make_unique<ApiAuth>(store);
    backend = std::make_unique<MemoryBackend>();
    QString error;
    ApiTokenGrant grant{"owner",
                        {"nb-a"},
                        {"notes:read", "notes:read-live", "notes:write", "notes:trash"},
                        std::nullopt};
    // Exercise HTTP authorization/revocation with an unlimited owner and finite readers.
    QCOMPARE(auth->issue(grant, &token, &error), ApiAuth::IssueResult::Issued);
    grant.expiresAt = QDateTime::currentDateTimeUtc().addDays(1);
    grant.scopes = {"notes:read"};
    QCOMPARE(auth->issue(grant, &reader, &error), ApiAuth::IssueResult::Issued);
    grant.notebookIds = {"nb-b"};
    QCOMPARE(auth->issue(grant, &other, &error), ApiAuth::IssueResult::Issued);
    QTcpServer reserve;
    QVERIFY(reserve.listen(QHostAddress::LocalHost, 0));
    port = reserve.serverPort();
    reserve.close();
    HttpServerOptions options;
    options.port = port;
    server = std::make_unique<ApiServer>(*backend, *auth);
    QVERIFY2(server->start(options, &error), qPrintable(error));
  }

  void cleanup() {
    server.reset();
    backend.reset();
    auth.reset();
  }

  void accessAuditCorrelatesAndRedacts() {
    AuditCapture audit;
    const auto stop = qScopeGuard([&] { server->stop(); });
    const QByteArray canary = "PRIVATE_AUDIT_CANARY";
    QList<Response> responses;
    responses.append(request("GET", "/api/v1/info", {}, "X-Request-Id: " + canary + "\r\n"));
    responses.append(request("GET", "/api/v1/info?private=" + canary));
    responses.append(request("GET", "/api/v1/info", {}, {}, QString::fromLatin1(canary)));
    responses.append(request("GET", "/api/v1/notebooks/" + canary + "/notes/" + canary));
    responses.append(request("POST", "/api/v1/search", "{\"query\":\"" + canary + "\"}"));
    responses.append(request(canary, "/api/v1/info"));
    responses.append(request("GET", "/api/v1/info", {}, "Host: " + canary + "\r\n"));
    responses.append(request("POST", "/api/v1/notebooks/nb-a/notes",
                             "{\"name\":\"" + canary + "\",\"format\":\"markdown\"}", {},
                             reader.secret));
    responses.append(
        request("GET", "/api/v1/info", {}, "Content-Length: 0\r\nContent-Length: 0\r\n"));
    server->stop();
    const auto records = audit.records("api.access");
    QCOMPARE(records.size(), responses.size());
    const QList<int> statuses{200, 400, 401, 404, 200, 400, 400, 403, 400};
    QSet<QString> ids;
    for (int i = 0; i < responses.size(); ++i) {
      QCOMPARE(responses[i].status, statuses[i]);
      QCOMPARE(responses[i].requestIdHeaders, 1);
      const auto requestId = QString::fromLatin1(responses[i].headers.value("x-request-id"));
      QCOMPARE(records[i].value("requestId").toString(), requestId);
      QCOMPARE(records[i].value("status").toInt(), statuses[i]);
      QVERIFY(records[i].value("responseWritten").toBool());
      QVERIFY(records[i].value("durationMs").toInteger(-1) >= 0);
      QVERIFY(QDateTime::fromString(records[i].value("timestamp").toString(), Qt::ISODateWithMs)
                  .isValid());
      if (responses[i].object().contains("requestId")) {
        QCOMPARE(responses[i].object().value("requestId").toString(), requestId);
      }
      ids.insert(requestId);
    }
    QCOMPARE(ids.size(), responses.size());
    QCOMPARE(records[0].value("tokenId").toString(), token.id);
    QCOMPARE(records[0].value("route").toString(), QStringLiteral("/api/v1/info"));
    QCOMPARE(records[1].value("route").toString(), QStringLiteral("/api/v1/info"));
    QVERIFY(records[2].value("tokenId").isNull());
    QVERIFY(records[2].value("route").isNull());
    QCOMPARE(records[3].value("route").toString(),
             QStringLiteral("/api/v1/notebooks/{notebookId}/notes/{noteId}"));
    QCOMPARE(records[4].value("route").toString(), QStringLiteral("/api/v1/search"));
    QCOMPARE(records[5].value("method").toString(), QStringLiteral("OTHER"));
    QVERIFY(records[6].value("tokenId").isNull());
    QCOMPARE(records[7].value("tokenId").toString(), reader.id);
    QVERIFY(records[8].value("tokenId").isNull());
    QVERIFY(!audit.text().contains(QString::fromLatin1(canary)));
    QVERIFY(!audit.text().contains(token.secret));
    QVERIFY(!audit.text().contains(reader.secret));
  }

  void auditOperationOutcomes() {
    AuditCapture audit;
    const auto stop = qScopeGuard([&] { server->stop(); });
    const auto created = request("POST", "/api/v1/notebooks/nb-a/notes",
                                 "{\"name\":\"PRIVATE_AUDIT_NAME.md\",\"format\":\"markdown\"}");
    QCOMPARE(created.status, 202);
    QCOMPARE(operation(created).object()["state"].toString(), QStringLiteral("succeeded"));
    QCOMPARE(backend->notes.size(), 5);
    backend->failSave = true;
    const auto failed = request("PUT", "/api/v1/notebooks/nb-a/notes/note-a/content",
                                "{\"text\":\"PRIVATE_AUDIT_BODY\"}",
                                match(backend->notes["note-a"].contentVersion()));
    QCOMPARE(failed.status, 202);
    QCOMPARE(operation(failed).object()["state"].toString(), QStringLiteral("failed"));
    QVERIFY(backend->notes["note-a"].dirty);
    QCOMPARE(backend->notes["note-a"].live, QStringLiteral("PRIVATE_AUDIT_BODY"));
    server->stop();
    const auto records = audit.records("api.operation");
    QCOMPARE(records.size(), 2);
    QCOMPARE(records[0].value("requestId").toString(),
             QString::fromLatin1(created.headers.value("x-request-id")));
    QCOMPARE(records[0].value("operationId").toString(), created.object()["id"].toString());
    QCOMPARE(records[0].value("state").toString(), QStringLiteral("succeeded"));
    QVERIFY(records[0].value("committed").toBool());
    QVERIFY(records[0].value("saved").toBool());
    QCOMPARE(records[1].value("requestId").toString(),
             QString::fromLatin1(failed.headers.value("x-request-id")));
    QCOMPARE(records[1].value("operationId").toString(), failed.object()["id"].toString());
    QCOMPARE(records[1].value("state").toString(), QStringLiteral("failed"));
    QCOMPARE(records[1].value("status").toInt(), 503);
    QVERIFY(records[1].value("outcomeKnown").toBool());
    QVERIFY(records[1].value("committed").toBool());
    QVERIFY(!records[1].value("saved").toBool());
    QVERIFY(!audit.text().contains("PRIVATE_AUDIT_"));
    QVERIFY(!audit.text().contains(token.secret));
  }

  void auditInterruptedOperations_data() {
    QTest::addColumn<bool>("committedBeforeStop");
    QTest::newRow("production-stopping-result") << false;
    QTest::newRow("committed-result-delivered-late") << true;
  }

  void auditInterruptedOperations() {
    QFETCH(bool, committedBeforeStop);
    AuditCapture audit;
    const auto stop = qScopeGuard([&] {
      if (server)
        server->stop();
    });
    backend->hold = true;
    const auto accepted = request("POST", "/api/v1/notebooks/nb-a/notes",
                                  "{\"name\":\"late.md\",\"format\":\"markdown\"}");
    QCOMPARE(accepted.status, 202);
    QTRY_COMPARE(backend->pending.size(), 1);
    auto pending = backend->pending.takeFirst();
    ApiResult result{{}, {503, "stopping", "The API stopped; reconcile any accepted mutation."}};
    if (committedBeforeStop) {
      result = backend->run(pending.command, pending.context);
      QCOMPARE(backend->notes.size(), 5);
      QVERIFY(result.committed && result.saved);
    }
    server->stop();
    server.reset();
    auth.reset();
    const auto interruptions = audit.records("api.operation_interrupted");
    QCOMPARE(interruptions.size(), 1);
    QVERIFY(!interruptions[0].value("outcomeKnown").toBool());
    QVERIFY(interruptions[0].value("committed").isNull());
    QVERIFY(interruptions[0].value("saved").isNull());
    QVERIFY(audit.records("api.operation").isEmpty());
    pending.completion(result);
    pending.completion(result);
    const auto records = audit.records("api.operation");
    QCOMPARE(records.size(), 1);
    QCOMPARE(records[0].value("operationId").toString(), accepted.object()["id"].toString());
    QCOMPARE(records[0].value("outcomeKnown").toBool(), committedBeforeStop);
    if (committedBeforeStop) {
      QVERIFY(records[0].value("committed").toBool());
      QVERIFY(records[0].value("saved").toBool());
      QCOMPARE(records[0].value("state").toString(), QStringLiteral("succeeded"));
    } else {
      QCOMPARE(backend->notes.size(), 4);
      QVERIFY(records[0].value("committed").isNull());
      QVERIFY(records[0].value("saved").isNull());
      QCOMPARE(records[0].value("state").toString(), QStringLiteral("interrupted"));
    }
  }

  void auditRevocationBeforeDispatch() {
    AuditCapture audit;
    const auto stop = qScopeGuard([&] { server->stop(); });
    auto future = std::async(std::launch::async, exchange, port,
                             wire("POST", "/api/v1/notebooks/nb-a/notes",
                                  "{\"name\":\"revoked.md\",\"format\":\"markdown\"}"));
    QVERIFY(future.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    const auto accepted = future.get();
    QCOMPARE(accepted.status, 202);
    QString error;
    QVERIFY(auth->revoke(token.id, &error));
    QCoreApplication::processEvents();
    server->stop();
    QCOMPARE(backend->calls, 0);
    const auto records = audit.records("api.operation");
    QCOMPARE(records.size(), 1);
    QCOMPARE(records[0].value("operationId").toString(), accepted.object()["id"].toString());
    QCOMPARE(records[0].value("state").toString(), QStringLiteral("cancelled"));
    QVERIFY(records[0].value("outcomeKnown").toBool());
    QVERIFY(!records[0].value("committed").toBool());
    QVERIFY(!records[0].value("saved").toBool());
    QVERIFY(audit.records("api.operation_interrupted").isEmpty());
  }

  void authenticationAndPreBodyAdmission() {
    QCOMPARE(request("GET", "/api/v1/info", {}, {}, "invalid").status, 401);
    QCOMPARE(request("GET", "/api/v1/info", {}, "Origin: https://example.test\r\n").status, 403);
    QCOMPARE(request("GET", "/api/v1/info", {}, "Cookie: auth=anything\r\n").status, 400);
    QCOMPARE(request("GET", "/api/v1/info", {}, "Host: evil.test\r\n").status, 400);
    QCOMPARE(request("GET", "/api/v1/info", {}, "Authorization: Bearer duplicate\r\n").status, 401);
    const auto response = request("GET", "/api/v1/info");
    QCOMPARE(response.status, 200);
    QCOMPARE(response.headers.value("cache-control"), QByteArray("no-store"));
    QCOMPARE(response.headers.value("x-content-type-options"), QByteArray("nosniff"));
    QVERIFY(!response.headers.value("x-request-id").isEmpty());
    QVERIFY(!response.headers.contains("access-control-allow-origin"));
    QVERIFY(!response.headers.contains("set-cookie"));
    // No body is sent despite a declared body; invalid auth must fail before reading it.
    auto raw = wire("POST", "/api/v1/search", {},
                    "Content-Type: application/json\r\nContent-Length: 999999\r\n", "invalid");
    auto future = std::async(std::launch::async, exchange, port, raw);
    QCOMPARE(wait(future).status, 401);
    QCOMPARE(backend->calls, 0);
  }

  void strictValidation() {
    QCOMPARE(request("OPTIONS", "/api/v1/info").status, 405);
    QCOMPARE(request("GET", "/api/v1/notebooks?unknown=x").status, 400);
    QCOMPARE(request("GET", "/api/v1/notebooks/nb-a/nodes?limit=1&limit=2").status, 400);
    QCOMPARE(request("GET", "/api/v1/notebooks/nb-a/nodes?limit=201").status, 400);
    QCOMPARE(request("GET", "/api/v1/notebooks/nb-a/notes/note-a/content?offset=1").status, 400);
    QCOMPARE(request("GET", "/api/v1/notebooks/nb-a/notes/%2e%2e").status, 400);
    QCOMPARE(
        request("POST", "/api/v1/search", "{\"query\":\"hello\",\"query\":\"private\"}").status,
        400);
    QCOMPARE(request("POST", "/api/v1/search", "{\"query\":\"hello\",\"\\u0071uery\":\"private\"}")
                 .status,
             400);
    QCOMPARE(request("POST", "/api/v1/search", "{\"query\":\"hello\",\"limit\":1.5}").status, 400);
    QCOMPARE(request("POST", "/api/v1/search", "{\"query\":\"hello\",\"regex\":true}").status, 400);
    QCOMPARE(request("POST", "/api/v1/search", "[]").status, 400);
    QCOMPARE(
        request("POST", "/api/v1/search", "{\"query\":\"hello\"}", "Content-Encoding: gzip\r\n")
            .status,
        415);
    QCOMPARE(request("POST", "/api/v1/notebooks/nb-a/notes",
                     "{\"name\":\"new.md\",\"format\":\"markdown\",\"text\":\"no\"}")
                 .status,
             400);
    QCOMPARE(
        request("PUT", "/api/v1/notebooks/nb-a/notes/note-a/content", "{\"text\":\"new\"}").status,
        428);
    QCOMPARE(request("PUT", "/api/v1/notebooks/nb-a/notes/note-a/content", "{\"text\":\"new\"}",
                     "If-Match: *\r\n")
                 .status,
             400);
    QCOMPARE(request("PUT", "/api/v1/notebooks/nb-a/notes/note-a/content", "{\"text\":\"new\"}",
                     "If-Match: W/\"weak\"\r\n")
                 .status,
             400);
    QCOMPARE(backend->calls, 0);
  }

  void resourceIsolationAndProtectedContent() {
    const auto notebooks = request("GET", "/api/v1/notebooks");
    QCOMPARE(notebooks.status, 200);
    QCOMPARE(notebooks.object()["items"].toArray().size(), 1);
    QVERIFY(!notebooks.bytes.contains("nb-b"));
    const auto nodes = request("GET", "/api/v1/notebooks/nb-a/nodes");
    QCOMPARE(nodes.status, 200);
    QVERIFY(!nodes.bytes.contains("classified"));
    QVERIFY(nodes.bytes.contains("photo.png"));
    QCOMPARE(request("GET", "/api/v1/notebooks/nb-b/notes/note-b").status, 404);
    QCOMPARE(request("GET", "/api/v1/notebooks/nb-a/notes/protected").status, 422);
    QCOMPARE(request("GET", "/api/v1/notebooks/nb-a/notes/binary/content").status, 422);
    const auto search = request("POST", "/api/v1/search", "{\"query\":\"words\"}");
    QCOMPARE(search.status, 200);
    QVERIFY(search.object()["items"].toArray().isEmpty());
    QCOMPARE(request("POST", "/api/v1/search", "{\"query\":\"private\",\"notebookIds\":[\"nb-b\"]}")
                 .status,
             404);
    QCOMPARE(request("GET", "/api/v1/notebooks/nb-a/notes/note-a/content?view=live", {}, {},
                     reader.secret)
                 .status,
             403);
    QCOMPARE(request("POST", "/api/v1/notebooks/nb-a/notes",
                     "{\"name\":\"new.md\",\"format\":\"markdown\"}", {}, reader.secret)
                 .status,
             403);
  }

  void savedLiveAndSnapshotPaging() {
    backend->notes["note-a"].dirty = true;
    backend->notes["note-a"].live = QStringLiteral("unsaved draft");
    const auto saved = request("GET", "/api/v1/notebooks/nb-a/notes/note-a/content");
    QCOMPARE(saved.status, 200);
    QCOMPARE(saved.object()["text"].toString(), QString::fromUtf8("hello é world"));
    QVERIFY(saved.object()["dirty"].toBool());
    const auto live = request("GET", "/api/v1/notebooks/nb-a/notes/note-a/content?view=live");
    QCOMPARE(live.object()["text"].toString(), QStringLiteral("unsaved draft"));
    QCOMPARE(live.object()["view"].toString(), QStringLiteral("live"));
    QVERIFY(!live.object().contains("noteStateVersion"));
    QCOMPARE(backend->notes["note-a"].text, QString::fromUtf8("hello é world"));
    const auto page = request("GET", "/api/v1/notebooks/nb-a/notes/note-a/content?limit=7");
    QCOMPARE(page.object()["text"].toString(), QStringLiteral("hello "));
    QCOMPARE(page.object()["returnedBytes"].toInt(), 6);
    const auto cursor = page.object()["nextCursor"].toString().toUtf8().toPercentEncoding();
    const auto next = request(
        "GET", "/api/v1/notebooks/nb-a/notes/note-a/content?offset=6&limit=2&cursor=" + cursor);
    QCOMPARE(next.object()["text"].toString(), QString::fromUtf8("é"));
    backend->notes["note-a"].text = QStringLiteral("changed on disk");
    QCOMPARE(request("GET", "/api/v1/notebooks/nb-a/notes/note-a/content?offset=6&cursor=" + cursor)
                 .status,
             412);
    backend->notes["note-a"].dirty = false;
    QCOMPARE(request("GET", "/api/v1/notebooks/nb-a/notes/note-a/content?view=live")
                 .object()["view"]
                 .toString(),
             QStringLiteral("saved"));
  }

  void mutationsConflictsAndOperationOwnership() {
    const auto content = request("GET", "/api/v1/notebooks/nb-a/notes/note-a/content");
    auto accepted =
        request("PUT", "/api/v1/notebooks/nb-a/notes/note-a/content", "{\"text\":\"replacement\"}",
                match(content.object()["contentVersion"].toString()));
    QCOMPARE(accepted.status, 202);
    const auto completed = operation(accepted);
    QCOMPARE(completed.status, 200);
    QCOMPARE(completed.object()["state"].toString(), QStringLiteral("succeeded"));
    QVERIFY(completed.object()["committed"].toBool());
    QVERIFY(completed.object()["saved"].toBool());
    QCOMPARE(backend->notes["note-a"].text, QStringLiteral("replacement"));
    QCOMPARE(request("GET", accepted.headers.value("location"), {}, {}, reader.secret).status, 404);
    accepted = request("POST", "/api/v1/notebooks/nb-a/notes/note-a/append", "{\"text\":\"!\"}",
                       match(backend->notes["note-a"].contentVersion()));
    QCOMPARE(operation(accepted).object()["state"].toString(), QStringLiteral("succeeded"));
    QCOMPARE(backend->notes["note-a"].text, QStringLiteral("replacement!"));
    accepted = request("PUT", "/api/v1/notebooks/nb-a/notes/note-a/content", "{\"text\":\"stale\"}",
                       match(content.object()["contentVersion"].toString()));
    QCOMPARE(operation(accepted).object()["error"].toObject()["status"].toInt(), 412);
    // The HTTP response-page ETag is never the saved-content precondition.
    accepted = request("PUT", "/api/v1/notebooks/nb-a/notes/note-a/content", "{\"text\":\"stale\"}",
                       "If-Match: " + content.headers.value("etag") + "\r\n");
    QCOMPARE(operation(accepted).object()["error"].toObject()["status"].toInt(), 412);
    accepted = request("PATCH", "/api/v1/notebooks/nb-a/notes/note-a", "{\"name\":\"renamed.md\"}",
                       match(backend->notes["note-a"].metadataVersion()));
    QCOMPARE(operation(accepted).object()["state"].toString(), QStringLiteral("succeeded"));
    QCOMPARE(backend->notes["note-a"].id, QStringLiteral("note-a"));
    QCOMPARE(backend->notes["note-a"].name, QStringLiteral("renamed.md"));
    accepted = request("DELETE", "/api/v1/notebooks/nb-a/notes/note-a", {},
                       match(backend->notes["note-a"].stateVersion()));
    QVERIFY(operation(accepted).object()["saved"].toBool());
    QVERIFY(!backend->notes.contains("note-a"));
    QCOMPARE(backend->trash["note-a"].text, QStringLiteral("replacement!"));
  }

  void createPartialInitializationAndDirtyConflict() {
    auto accepted = request("POST", "/api/v1/notebooks/nb-a/notes",
                            "{\"name\":\"empty.md\",\"format\":\"markdown\"}");
    QCOMPARE(accepted.status, 202);
    const auto created = operation(accepted).object()["resource"].toObject();
    const auto createdId = created["id"].toString();
    QVERIFY(!createdId.isEmpty());
    QVERIFY(backend->notes[createdId].text.isEmpty());
    accepted = request("POST", "/api/v1/notebooks/nb-a/notes",
                       "{\"name\":\"empty.md\",\"format\":\"markdown\"}");
    QCOMPARE(operation(accepted).object()["error"].toObject()["status"].toInt(), 409);
    accepted = request("PUT", "/api/v1/notebooks/nb-a/notes/" + createdId.toLatin1() + "/content",
                       "{\"text\":\"initialize\"}", match("saved-stale"));
    QCOMPARE(operation(accepted).object()["error"].toObject()["status"].toInt(), 412);
    QVERIFY(backend->notes[createdId].text.isEmpty());
    backend->notes["note-a"].dirty = true;
    accepted = request("PUT", "/api/v1/notebooks/nb-a/notes/note-a/content", "{\"text\":\"new\"}",
                       match(backend->notes["note-a"].contentVersion()));
    QCOMPARE(operation(accepted).object()["error"].toObject()["code"].toString(),
             QStringLiteral("unsaved_changes"));
    backend->notes["note-a"].dirty = false;
    backend->failSave = true;
    accepted =
        request("PUT", "/api/v1/notebooks/nb-a/notes/note-a/content",
                "{\"text\":\"changed buffer\"}", match(backend->notes["note-a"].contentVersion()));
    const auto failedSave = operation(accepted).object();
    QCOMPARE(failedSave["state"].toString(), QStringLiteral("failed"));
    QVERIFY(failedSave["committed"].toBool());
    QVERIFY(!failedSave["saved"].toBool());
    QCOMPARE(backend->notes["note-a"].text, QString::fromUtf8("hello é world"));
    backend->notes["note-a"].dirty = false;
    backend->failAfterSave = true;
    accepted = request("PUT", "/api/v1/notebooks/nb-a/notes/note-a/content",
                       "{\"text\":\"persisted despite hook failure\"}",
                       match(backend->notes["note-a"].contentVersion()));
    const auto failedHook = operation(accepted).object();
    QCOMPARE(failedHook["state"].toString(), QStringLiteral("failed"));
    QCOMPARE(failedHook["error"].toObject()["code"].toString(),
             QStringLiteral("after_hook_failed"));
    QVERIFY(failedHook["committed"].toBool());
    QVERIFY(failedHook["saved"].toBool());
    QCOMPARE(backend->notes["note-a"].text, QStringLiteral("persisted despite hook failure"));
  }

  void revocationAndBoundedOperations() {
    backend->hold = true;
    Response accepted;
    for (int i = 0; i < 4; ++i) {
      accepted = request("POST", "/api/v1/notebooks/nb-a/notes",
                         "{\"name\":\"queued.md\",\"format\":\"markdown\"}");
      QCOMPARE(accepted.status, 202);
    }
    QCOMPARE(request("POST", "/api/v1/notebooks/nb-a/notes",
                     "{\"name\":\"queued.md\",\"format\":\"markdown\"}")
                 .status,
             429);
    QTRY_COMPARE(backend->pending.size(), 4);
    QString error;
    QVERIFY(auth->revoke(token.id, &error));
    backend->release();
    QCOMPARE(backend->notes.size(), 4);
    QCOMPARE(request("GET", accepted.headers.value("location")).status, 401);
    QCOMPARE(request("GET", accepted.headers.value("location"), {}, {}, reader.secret).status, 404);
  }

  void revokedReadAndLateCompletionAfterShutdown() {
    backend->hold = true;
    auto read = std::async(std::launch::async, exchange, port,
                           wire("GET", "/api/v1/notebooks/nb-a/notes/note-a/content"));
    QTRY_COMPARE(backend->pending.size(), 1);
    QString error;
    QVERIFY(auth->revoke(token.id, &error));
    // Even a backend that captured data before revocation cannot publish it afterward.
    auto pending = backend->pending.takeFirst();
    pending.completion({{{"text", "must not escape"}}});
    const auto denied = wait(read);
    QCOMPARE(denied.status, 401);
    QVERIFY(!denied.bytes.contains("must not escape"));
    auto second = std::async(
        std::launch::async, exchange, port,
        wire("GET", "/api/v1/notebooks/nb-a/notes/note-a/content", {}, {}, reader.secret));
    QTRY_COMPARE(backend->pending.size(), 1);
    auto retained = backend->pending.takeFirst();
    QElapsedTimer elapsed;
    elapsed.start();
    server->stop();
    QVERIFY(elapsed.elapsed() < 3000);
    server.reset();
    auth.reset();
    QVERIFY(!retained.context.authorized());
    retained.completion({{{"text", "late result"}}});
    const auto stopped = wait(second);
    QVERIFY(stopped.status == 0 || stopped.status == 503);
    QVERIFY(!stopped.bytes.contains("late result"));
  }

  void boundedSearchAdmissionAndDeadline() {
    backend->hold = true;
    std::vector<std::future<Response>> searches;
    for (int i = 0; i < 3; ++i) {
      searches.push_back(std::async(std::launch::async, exchange, port,
                                    wire("POST", "/api/v1/search", "{\"query\":\"hello\"}")));
    }
    QTRY_COMPARE(backend->pending.size(), 3);
    QCOMPARE(request("POST", "/api/v1/search", "{\"query\":\"hello\"}").status, 429);
    backend->release();
    for (auto &search : searches) {
      QCOMPARE(wait(search).status, 200);
    }
    auto timed = std::async(std::launch::async, exchange, port,
                            wire("GET", "/api/v1/notebooks/nb-a/notes/note-a/content"));
    QTRY_COMPARE(backend->pending.size(), 1);
    const auto expired = wait(timed);
    QCOMPARE(expired.status, 503);
    QCOMPARE(expired.object()["code"].toString(), QStringLiteral("deadline_exceeded"));
    QVERIFY(backend->pending.first().context.cancelled->load());
    backend->release();
  }

  void globalOperationAndRetentionBounds() {
    ApiOperations operations(server->instanceId());
    ApiError error;
    QStringList active;
    auto authorized = [] { return true; };
    for (int i = 0; i < 16; ++i) {
      const auto id = operations.admit({{}, QString::number(i / 4), {}, {}}, authorized,
                                       std::make_shared<std::atomic_bool>(false), &error);
      QVERIFY(!id.isEmpty());
      active.append(id);
    }
    QVERIFY(operations
                .admit({{}, QStringLiteral("fifth-owner"), {}, {}}, authorized,
                       std::make_shared<std::atomic_bool>(false), &error)
                .isEmpty());
    QCOMPARE(error.status, 429);
    for (const auto &id : active) {
      operations.complete(id, {{{"id", "note-a"}, {"notebookId", "nb-a"}}, {}, true, true});
    }
    QString first;
    for (int i = 0; i < 257; ++i) {
      error = {};
      const auto id = operations.admit({{}, QStringLiteral("owner"), {}, {}}, authorized,
                                       std::make_shared<std::atomic_bool>(false), &error);
      QVERIFY(!id.isEmpty());
      if (i == 0) {
        first = id;
      }
      QVERIFY(operations.running(id));
      operations.complete(id, {{{"id", "note-a"}}, {}, true, true});
      QCOMPARE(operations.lookup(id, "different-owner").error.status, 404);
      // Distinct terminal timestamps make retention ordering deterministic.
      QTest::qWait(1);
    }
    QCOMPARE(operations.lookup(first, "owner").error.status, 410);
    operations.stop();
    QVERIFY(operations
                .admit({{}, QStringLiteral("owner"), {}, {}}, authorized,
                       std::make_shared<std::atomic_bool>(false), &error)
                .isEmpty());
    QCOMPARE(error.status, 503);
  }

  void rejectsDeviceNamesBeforeMutation() {
    for (const auto &name :
         {QStringLiteral("CON.md"), QStringLiteral("nUl.md"), QStringLiteral("COM1.md"),
          QStringLiteral("LPT9.md"), QStringLiteral("COM\u00b9.md")}) {
      const auto body = QJsonDocument(QJsonObject{{"name", name}, {"format", "markdown"}})
                            .toJson(QJsonDocument::Compact);
      QCOMPARE(request("POST", "/api/v1/notebooks/nb-a/notes", body).status, 400);
      const auto rename = QJsonDocument(QJsonObject{{"name", name}}).toJson(QJsonDocument::Compact);
      QCOMPARE(request("PATCH", "/api/v1/notebooks/nb-a/notes/note-a", rename,
                       match(backend->notes["note-a"].metadataVersion()))
                   .status,
               400);
    }
    QCOMPARE(backend->notes["note-a"].name, QStringLiteral("hello.md"));
    const auto accepted = request("POST", "/api/v1/notebooks/nb-a/notes",
                                  "{\"name\":\"COM10.md\",\"format\":\"markdown\"}");
    QCOMPARE(accepted.status, 202);
    const auto result = operation(accepted).object();
    QCOMPARE(result["state"].toString(), QStringLiteral("succeeded"));
    const auto id = result["resource"].toObject()["id"].toString();
    QCOMPARE(
        request("GET", "/api/v1/notebooks/nb-a/notes/" + id.toLatin1()).object()["name"].toString(),
        QStringLiteral("COM10.md"));
  }

  void stopPreventsQueuedDispatch() {
    AuditCapture audit;
    const auto stop = qScopeGuard([&] { server->stop(); });
    auto admission = std::async(std::launch::async, exchange, port,
                                wire("POST", "/api/v1/notebooks/nb-a/notes",
                                     "{\"name\":\"not-created.md\",\"format\":\"markdown\"}"));
    // Mutation admission needs no GUI execution; deliberately do not pump queued dispatch here.
    QVERIFY(admission.wait_for(std::chrono::seconds(3)) == std::future_status::ready);
    QCOMPARE(admission.get().status, 202);
    QCOMPARE(backend->calls, 0);
    server->stop();
    QCoreApplication::processEvents();
    QCOMPARE(backend->calls, 0);
    QCOMPARE(backend->notes.size(), 4);
    const auto records = audit.records("api.operation");
    QCOMPARE(records.size(), 1);
    QCOMPARE(records[0].value("state").toString(), QStringLiteral("cancelled"));
    QVERIFY(records[0].value("outcomeKnown").toBool());
    QVERIFY(!records[0].value("committed").toBool());
    QVERIFY(!records[0].value("saved").toBool());
    QVERIFY(audit.records("api.operation_interrupted").isEmpty());
  }

  void lifecycleRecreationAndOldOperations() {
    auto accepted = request("POST", "/api/v1/notebooks/nb-a/notes",
                            "{\"name\":\"created.md\",\"format\":\"markdown\"}");
    const auto oldPath = accepted.headers.value("location");
    const auto oldInstance = server->instanceId();
    server->stop();
    HttpServerOptions options;
    options.port = port;
    QString error;
    QVERIFY(!server->start(options, &error));
    server.reset();
    backend = std::make_unique<MemoryBackend>();
    server = std::make_unique<ApiServer>(*backend, *auth);
    QVERIFY2(server->start(options, &error), qPrintable(error));
    QVERIFY(server->instanceId() != oldInstance);
    QCOMPARE(request("GET", oldPath).status, 410);
    QCOMPARE(request("GET", "/api/v1/info").status, 200);
  }
};

QTEST_GUILESS_MAIN(TestRestApi)
#include "test_restapi.moc"
