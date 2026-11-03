#include <QtTest>

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTcpSocket>

#include <http/httpserver.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace vnotex;

namespace {

HttpServerOptions options() {
  HttpServerOptions result;
  result.port = 0;
  result.ioTimeoutSeconds = 1;
  return result;
}

HttpResponse ok(const HttpRequest &) {
  HttpResponse response;
  response.contentType = "text/plain";
  response.body = "complete";
  return response;
}

bool connectSocket(QTcpSocket &p_socket, quint16 p_port,
                   const QHostAddress &p_address = QHostAddress::LocalHost) {
  p_socket.connectToHost(p_address, p_port);
  return p_socket.waitForConnected(2000);
}

bool sendBytes(QTcpSocket &p_socket, const QByteArray &p_bytes) {
  if (p_socket.write(p_bytes) != p_bytes.size()) {
    return false;
  }
  return p_socket.bytesToWrite() == 0 || p_socket.waitForBytesWritten(2000);
}

QByteArray receiveClosed(QTcpSocket &p_socket, int p_timeout = 3000) {
  QDeadlineTimer deadline(p_timeout);
  QByteArray response = p_socket.readAll();
  while (p_socket.state() != QAbstractSocket::UnconnectedState && !deadline.hasExpired()) {
    p_socket.waitForReadyRead(static_cast<int>(deadline.remainingTime()));
    response += p_socket.readAll();
  }
  return response;
}

QByteArray exchange(quint16 p_port, const QByteArray &p_request) {
  QTcpSocket socket;
  if (!connectSocket(socket, p_port) || !sendBytes(socket, p_request)) {
    return {};
  }
  return receiveClosed(socket);
}

int status(const QByteArray &p_response) {
  const auto parts = p_response.split(' ');
  return parts.size() >= 2 ? parts[1].toInt() : 0;
}

QByteArray body(const QByteArray &p_response) {
  const auto at = p_response.indexOf("\r\n\r\n");
  return at < 0 ? QByteArray() : p_response.mid(at + 4);
}

bool waitCount(const std::atomic_int &p_count, int p_expected, int p_timeout = 2000) {
  QDeadlineTimer deadline(p_timeout);
  while (p_count.load() < p_expected && !deadline.hasExpired()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return p_count.load() >= p_expected;
}

struct AccessRecords {
  std::mutex mutex;
  QList<HttpAccessRecord> events;
  void append(const HttpAccessRecord &p_record) {
    std::lock_guard lock(mutex);
    events.append(p_record);
  }
  QList<HttpAccessRecord> snapshot() {
    std::lock_guard lock(mutex);
    return events;
  }
};

struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  bool released = false;
  void wait() {
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait_for(lock, std::chrono::seconds(5), [this] { return released; });
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};

struct ReleaseGate {
  Gate &gate;
  ~ReleaseGate() { gate.release(); }
};

} // namespace

class TestHttpServer : public QObject {
  Q_OBJECT

private slots:
  void rawValuesAndFragmentation();
  void admissionPrecedesBody();
  void acceptedContinueAndBinaryBody();
  void malformed_data();
  void malformed();
  void limits();
  void trailersShareHeaderBudget();
  void slowHeaderDeadline();
  void slowBodyDeadline();
  void stopInterruptsSockets();
  void stopInterruptsSlowReader();
  void boundedWorkersAndQueue();
  void lifecycleAndBindConflict();
  void literalLoopbackValidation();
  void ipv6Loopback();
  void keepAliveAndHead();
  void callbackExceptionsAreContained();
  void accessRecordsCoverRejectedAndPartialRequests();
  void accessRecordsIgnoreIdleEmptyLines();
};

void TestHttpServer::rawValuesAndFragmentation() {
  HttpRequest admitted, handled;
  std::atomic_int admissions{0}, handlers{0};
  HttpServer server;
  QString error;
  QVERIFY2(server.start(
               options(),
               [&](const HttpRequest &p_request) -> std::optional<HttpResponse> {
                 admitted = p_request;
                 ++admissions;
                 return std::nullopt;
               },
               [&](const HttpRequest &p_request) {
                 handled = p_request;
                 ++handlers;
                 return ok(p_request);
               },
               &error),
           qPrintable(error));
  QTcpSocket socket;
  QVERIFY(connectSocket(socket, server.port()));
  const QList<QByteArray> fragments = {
      "PO",
      "ST /a%2Fb?x=%252f&x=+ HTTP/1.1\r",
      "\nHost: localhost\r\nX-Value: first\r\n",
      "x-value: second\r\nContent-Length: 5\r\nConnection: close\r\n\r",
      "\nhe",
      "llo"};
  for (const auto &fragment : fragments) {
    QVERIFY(sendBytes(socket, fragment));
  }
  QCOMPARE(status(receiveClosed(socket)), 200);
  server.stop();
  QCOMPARE(admissions.load(), 1);
  QCOMPARE(handlers.load(), 1);
  QVERIFY(admitted.body.isEmpty());
  QCOMPARE(handled.body, QByteArray("hello"));
  QCOMPARE(handled.target, QByteArray("/a%2Fb?x=%252f&x=+"));
  QCOMPARE(handled.method, QByteArray("POST"));
  QCOMPARE(handled.headerCount("X-VALUE"), 2);
  QCOMPARE(handled.header("x-value"), QByteArray("first"));
  QCOMPARE(handled.headers[1].first, QByteArray("X-Value"));
  QCOMPARE(handled.headers[2].first, QByteArray("x-value"));
  QCOMPARE(handled.headers[2].second, QByteArray("second"));
  QCOMPARE(handled.peerAddress, QStringLiteral("127.0.0.1"));
}

void TestHttpServer::admissionPrecedesBody() {
  std::atomic_int handlers{0}, admissions{0};
  HttpServer server;
  QVERIFY(server.start(
      options(),
      [&](const HttpRequest &p_request) -> std::optional<HttpResponse> {
        if (!p_request.body.isEmpty()) {
          return HttpResponse{500};
        }
        ++admissions;
        HttpResponse response;
        response.status = 401;
        response.body = "not admitted";
        return response;
      },
      [&](const HttpRequest &p_request) {
        ++handlers;
        return ok(p_request);
      },
      nullptr));
  for (const auto &framing : {QByteArray("Content-Length: 999999999\r\n"),
                              QByteArray("Transfer-Encoding: chunked\r\n")}) {
    QTcpSocket socket;
    QVERIFY(connectSocket(socket, server.port()));
    QElapsedTimer timer;
    timer.start();
    QVERIFY(sendBytes(socket, "POST / HTTP/1.1\r\nHost: localhost\r\n" + framing +
                                  "Expect: 100-continue\r\n\r\n"));
    const auto response = receiveClosed(socket);
    QCOMPARE(status(response), 401);
    QVERIFY(!response.contains("100 Continue"));
    QVERIFY(timer.elapsed() < 900);
    QCOMPARE(socket.state(), QAbstractSocket::UnconnectedState);
  }
  QCOMPARE(admissions.load(), 2);
  QCOMPARE(handlers.load(), 0);
}

void TestHttpServer::acceptedContinueAndBinaryBody() {
  HttpRequest captured;
  HttpServer server;
  QVERIFY(server.start(
      options(), {},
      [&](const HttpRequest &p_request) {
        captured = p_request;
        return ok(p_request);
      },
      nullptr));
  QTcpSocket socket;
  QVERIFY(connectSocket(socket, server.port()));
  QVERIFY(sendBytes(socket, "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n"
                            "Expect: 100-continue\r\nConnection: close\r\n\r\n"));
  QByteArray interim;
  QDeadlineTimer deadline(2000);
  while (!interim.contains("\r\n\r\n") && !deadline.hasExpired()) {
    socket.waitForReadyRead(static_cast<int>(deadline.remainingTime()));
    interim += socket.readAll();
  }
  QCOMPARE(interim, QByteArray("HTTP/1.1 100 Continue\r\n\r\n"));
  const QByteArray binary("a\0b\r\n", 5);
  QVERIFY(sendBytes(socket, binary));
  QCOMPARE(status(receiveClosed(socket)), 200);
  server.stop();
  QCOMPARE(captured.body, binary);
}

void TestHttpServer::malformed_data() {
  QTest::addColumn<QByteArray>("request");
  const QByteArray start = "POST / HTTP/1.1\r\nHost: localhost\r\n";
  QTest::newRow("duplicate equal length")
      << start + "Content-Length: 0\r\ncontent-length: 0\r\n\r\n";
  QTest::newRow("duplicate unequal length")
      << start + "Content-Length: 0\r\nContent-Length: 1\r\n\r\nx";
  QTest::newRow("comma length") << start + "Content-Length: 0, 0\r\n\r\n";
  QTest::newRow("negative length") << start + "Content-Length: -1\r\n\r\n";
  QTest::newRow("overflow length") << start + "Content-Length: 18446744073709551616\r\n\r\n";
  QTest::newRow("zero length and chunked")
      << start + "Content-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n";
  QTest::newRow("duplicate transfer encoding")
      << start + "Transfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n";
  QTest::newRow("unsupported transfer encoding")
      << start + "Transfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n";
  QTest::newRow("bad chunk length")
      << start + "Transfer-Encoding: chunked\r\n\r\nxyz\r\nx\r\n0\r\n\r\n";
  QTest::newRow("bad chunk delimiter")
      << start + "Transfer-Encoding: chunked\r\n\r\n1\r\nxX\r\n0\r\n\r\n";
  QTest::newRow("missing terminal CRLF") << start + "Transfer-Encoding: chunked\r\n\r\n0\r\n";
  QTest::newRow("upgrade") << start + "Connection: upgrade\r\nUpgrade: h2c\r\n\r\n";
  QTest::newRow("upgrade duplicate connection")
      << start + "Connection: keep-alive\r\nConnection: UpGrAdE\r\n\r\n";
  QTest::newRow("absolute target")
      << QByteArray("GET http://localhost/ HTTP/1.1\r\nHost: localhost\r\n\r\n");
  QTest::newRow("fragment target")
      << QByteArray("GET /x#fragment HTTP/1.1\r\nHost: localhost\r\n\r\n");
  QTest::newRow("HTTP 1.0") << QByteArray("GET / HTTP/1.0\r\nHost: localhost\r\n\r\n");
  QTest::newRow("duplicate host") << start + "Host: localhost\r\n\r\n";
  QTest::newRow("bare LF") << start + "X-One: a\nX-Two: b\r\n\r\n";
  QTest::newRow("obsolete folding") << start + "X-One: a\r\n more\r\n\r\n";
  QTest::newRow("nul header") << start + QByteArray("X-One: a\0b\r\n\r\n", 15);
  QTest::newRow("compressed body") << start + "Content-Encoding: gzip\r\nContent-Length: 0\r\n\r\n";
  QTest::newRow("unknown expectation") << start + "Expect: magic\r\n\r\n";
}

void TestHttpServer::malformed() {
  QFETCH(QByteArray, request);
  std::atomic_int handlers{0};
  HttpServer server;
  QVERIFY(server.start(
      options(), {},
      [&](const HttpRequest &p_request) {
        ++handlers;
        return ok(p_request);
      },
      nullptr));
  QTcpSocket socket;
  QVERIFY(connectSocket(socket, server.port()));
  QVERIFY(sendBytes(socket, request));
  const auto response = receiveClosed(socket);
  QVERIFY2(response.isEmpty() || status(response) >= 400, response.constData());
  QCOMPARE(socket.state(), QAbstractSocket::UnconnectedState);
  QCOMPARE(handlers.load(), 0);
}

void TestHttpServer::limits() {
  std::atomic_int handlers{0};
  QByteArray captured;
  HttpServer server;
  auto config = options();
  config.maxBodyBytes = 8;
  QVERIFY(server.start(
      config, {},
      [&](const HttpRequest &p_request) {
        captured = p_request.body;
        ++handlers;
        return ok(p_request);
      },
      nullptr));
  const QByteArray start = "POST / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n";
  QCOMPARE(status(exchange(server.port(), start + "Content-Length: 9\r\n\r\n")), 413);
  QCOMPARE(status(exchange(server.port(), start + "Content-Length: 8\r\n\r\n12345678")), 200);
  QCOMPARE(status(exchange(server.port(), start + "Transfer-Encoding: chunked\r\n\r\n"
                                                  "4\r\n1234\r\n4\r\n5678\r\n0\r\n\r\n")),
           200);
  QCOMPARE(status(exchange(server.port(), start + "Transfer-Encoding: chunked\r\n\r\n"
                                                  "4\r\n1234\r\n5\r\n56789\r\n0\r\n\r\n")),
           413);
  QCOMPARE(status(exchange(server.port(), start + "Transfer-Encoding: chunked\r\n\r\n"
                                                  "FFFFFFFFFFFFFFFF\r\nx")),
           413);
  QByteArray fields;
  for (int i = 0; i < 20; ++i) {
    fields += "X-Field: " + QByteArray(900, 'x') + "\r\n";
  }
  const auto oversizedHeaders = exchange(server.port(), start + fields + "\r\n");
  QVERIFY(oversizedHeaders.isEmpty() || status(oversizedHeaders) >= 400);
  const QByteArray headerStart = "GET / HTTP/1.1\r\nHost: localhost\r\n"
                                 "Connection: close\r\nX-Padding: ";
  const auto existingHeaderBytes = headerStart.size() - headerStart.indexOf("\r\n") - 2;
  const auto paddingSize = 16384 - existingHeaderBytes - 4;
  QCOMPARE(status(exchange(server.port(), headerStart + QByteArray(paddingSize, 'h') + "\r\n\r\n")),
           200);
  const auto oneByteOver =
      exchange(server.port(), headerStart + QByteArray(paddingSize + 1, 'h') + "\r\n\r\n");
  QVERIFY(oneByteOver.isEmpty() || status(oneByteOver) >= 400);
  const auto oversizedTarget = exchange(server.port(), "GET /" + QByteArray(4096, 'x') +
                                                           " HTTP/1.1\r\nHost: localhost\r\n\r\n");
  QVERIFY(oversizedTarget.isEmpty() || status(oversizedTarget) >= 400);
  const auto allowedTarget =
      exchange(server.port(), "GET /" + QByteArray(4095, 'x') +
                                  " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
  QCOMPARE(status(allowedTarget), 200);
  server.stop();
  QCOMPARE(handlers.load(), 4);
}

void TestHttpServer::trailersShareHeaderBudget() {
  std::atomic_int handlers{0};
  HttpServer server;
  QVERIFY(server.start(
      options(), {},
      [&](const HttpRequest &p_request) {
        ++handlers;
        return ok(p_request);
      },
      nullptr));
  const QByteArray start = "POST / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n"
                           "Transfer-Encoding: chunked\r\nTrailer: X-Final\r\n";
  QCOMPARE(status(exchange(server.port(), start + "\r\n1\r\nx\r\n0\r\nX-Final: done\r\n\r\n")),
           200);
  const auto response = exchange(
      server.port(), start + "X-Padding: " + QByteArray(10000, 'a') +
                         "\r\n\r\n1\r\nx\r\n0\r\nX-Final: " + QByteArray(10000, 'b') + "\r\n\r\n");
  QVERIFY(response.isEmpty() || status(response) >= 400);
  QCOMPARE(handlers.load(), 1);
}

void TestHttpServer::slowHeaderDeadline() {
  std::atomic_int handlers{0};
  HttpServer server;
  QVERIFY(server.start(
      options(), {},
      [&](const HttpRequest &p_request) {
        ++handlers;
        return ok(p_request);
      },
      nullptr));
  QTcpSocket socket;
  QVERIFY(connectSocket(socket, server.port()));
  QVERIFY(sendBytes(socket, "GET / HTTP/1.1\r\nHost: localhost\r\nX-Slow: "));
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < 1600 && socket.state() == QAbstractSocket::ConnectedState) {
    socket.write("a", 1);
    socket.flush();
    socket.waitForReadyRead(180);
    socket.readAll();
  }
  receiveClosed(socket, 1000);
  QCOMPARE(socket.state(), QAbstractSocket::UnconnectedState);
  QVERIFY(timer.elapsed() < 2200);
  QCOMPARE(handlers.load(), 0);
}

void TestHttpServer::slowBodyDeadline() {
  std::atomic_int admissions{0}, handlers{0};
  HttpServer server;
  QVERIFY(server.start(
      options(),
      [&](const HttpRequest &) -> std::optional<HttpResponse> {
        ++admissions;
        return std::nullopt;
      },
      [&](const HttpRequest &p_request) {
        ++handlers;
        return ok(p_request);
      },
      nullptr));
  for (bool chunked : {false, true}) {
    QTcpSocket socket;
    QVERIFY(connectSocket(socket, server.port()));
    const QByteArray framing =
        chunked ? "Transfer-Encoding: chunked\r\n\r\n64\r\n" : "Content-Length: 100\r\n\r\n";
    QVERIFY(sendBytes(socket, "POST / HTTP/1.1\r\nHost: localhost\r\n" + framing));
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1600 && socket.state() == QAbstractSocket::ConnectedState) {
      socket.write("a", 1);
      socket.flush();
      socket.waitForReadyRead(180);
      socket.readAll();
    }
    receiveClosed(socket, 1000);
    QCOMPARE(socket.state(), QAbstractSocket::UnconnectedState);
    QVERIFY(timer.elapsed() < 2200);
  }
  QCOMPARE(admissions.load(), 2);
  QCOMPARE(handlers.load(), 0);
}

void TestHttpServer::stopInterruptsSockets() {
  HttpServer server;
  auto config = options();
  config.ioTimeoutSeconds = 30;
  config.keepAliveSeconds = 30;
  config.workerCount = 3;
  QVERIFY(server.start(config, {}, ok, nullptr));
  QTcpSocket header, bodySocket, idle, queued;
  QVERIFY(connectSocket(header, server.port()));
  QVERIFY(sendBytes(header, "GET / HTTP/1.1\r\nHost: local"));
  QVERIFY(connectSocket(bodySocket, server.port()));
  QVERIFY(
      sendBytes(bodySocket, "POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 100\r\n\r\nx"));
  QVERIFY(connectSocket(idle, server.port()));
  QVERIFY(connectSocket(queued, server.port()));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  QElapsedTimer timer;
  timer.start();
  server.stop(); // The owner thread has never pumped the Qt event loop.
  QVERIFY(timer.elapsed() < 1500);
  QVERIFY(!server.isListening());
  QCOMPARE(server.port(), quint16(0));
  for (auto socket : {&header, &bodySocket, &idle, &queued}) {
    receiveClosed(*socket, 1000);
    QCOMPARE(socket->state(), QAbstractSocket::UnconnectedState);
  }
  server.stop();
}

void TestHttpServer::stopInterruptsSlowReader() {
  AccessRecords records;
  std::atomic_int handlers{0};
  HttpServer server;
  auto config = options();
  config.ioTimeoutSeconds = 30;
  config.accessObserver = [&](const HttpAccessRecord &p_record) { records.append(p_record); };
  QVERIFY(server.start(
      config, {},
      [&](const HttpRequest &) {
        HttpResponse response;
        response.body = QByteArray(16 * 1024 * 1024, 'x');
        ++handlers;
        return response;
      },
      nullptr));
  QTcpSocket socket;
  socket.setReadBufferSize(1024);
  QVERIFY(connectSocket(socket, server.port()));
  QVERIFY(sendBytes(socket, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
  QVERIFY(waitCount(handlers, 1));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  QElapsedTimer timer;
  timer.start();
  server.stop();
  QVERIFY(timer.elapsed() < 1500);
  const auto events = records.snapshot();
  QCOMPARE(events.size(), 1);
  QCOMPARE(events[0].status, 200);
  QVERIFY(!events[0].responseWritten);
}

void TestHttpServer::boundedWorkersAndQueue() {
  AccessRecords records;
  Gate gate;
  std::atomic_int entered{0}, active{0}, maximum{0};
  HttpServer server;
  auto config = options();
  config.workerCount = 2;
  config.maxQueuedConnections = 1;
  config.accessObserver = [&](const HttpAccessRecord &p_record) { records.append(p_record); };
  QVERIFY(server.start(
      config, {},
      [&](const HttpRequest &p_request) {
        const int running = ++active;
        int observed = maximum.load();
        while (running > observed && !maximum.compare_exchange_weak(observed, running)) {
        }
        ++entered;
        gate.wait();
        --active;
        return ok(p_request);
      },
      nullptr));
  ReleaseGate release{gate};
  QTcpSocket first, second, queued, overloaded;
  const QByteArray request = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
  QVERIFY(connectSocket(first, server.port()));
  QVERIFY(sendBytes(first, request));
  QVERIFY(waitCount(entered, 1));
  QVERIFY(connectSocket(second, server.port()));
  QVERIFY(sendBytes(second, request));
  QVERIFY(waitCount(entered, 2));
  QVERIFY(connectSocket(queued, server.port()));
  QVERIFY(sendBytes(queued, request));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  QVERIFY(connectSocket(overloaded, server.port()));
  sendBytes(overloaded, request); // The accept loop may already have rejected it.
  const auto rejected = receiveClosed(overloaded, 1000);
  QCOMPARE(overloaded.state(), QAbstractSocket::UnconnectedState);
  QVERIFY(rejected.isEmpty() || status(rejected) == 503 || status(rejected) == 429);
  QCOMPARE(entered.load(), 2);
  gate.release();
  QCOMPARE(status(receiveClosed(first)), 200);
  QCOMPARE(status(receiveClosed(second)), 200);
  QCOMPARE(status(receiveClosed(queued)), 200);
  server.stop();
  QCOMPARE(entered.load(), 3);
  QCOMPARE(maximum.load(), 2);
  const auto events = records.snapshot();
  QCOMPARE(events.size(), 4);
  QCOMPARE(std::count_if(events.cbegin(), events.cend(),
                         [](const HttpAccessRecord &p_event) {
                           return p_event.rejection == HttpAccessRecord::Rejection::QueueFull &&
                                  p_event.status == 0 && p_event.context.requestId.isEmpty() &&
                                  !p_event.responseWritten;
                         }),
           1);
}

void TestHttpServer::lifecycleAndBindConflict() {
  const QByteArray request = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
  HttpServer first, second;
  QString error;
  QVERIFY(first.start(options(), {}, ok, &error));
  QVERIFY(error.isEmpty());
  const quint16 port = first.port();
  QVERIFY(port != 0);
  auto conflict = options();
  conflict.port = port;
  QVERIFY(!second.start(conflict, {}, ok, &error));
  QVERIFY(!error.isEmpty());
  QVERIFY(!second.isListening());
  QCOMPARE(second.port(), quint16(0));
  QVERIFY(!first.start(options(), {}, ok, &error));
  QCOMPARE(status(exchange(first.port(), request)), 200);
  first.stop();
  QVERIFY(second.start(conflict, {}, ok, &error));
  QCOMPARE(second.port(), port);
  QCOMPARE(status(exchange(second.port(), request)), 200);
  second.stop();
  for (int i = 0; i < 5; ++i) {
    QVERIFY(first.start(options(), {}, ok, &error));
    QCOMPARE(status(exchange(first.port(), request)), 200);
    first.stop();
  }
}

void TestHttpServer::literalLoopbackValidation() {
  HttpServer server;
  for (const auto address : {"0.0.0.0", "::", "localhost", "127.1", "127.0.0.01", "127.0.0.1%1",
                             "::1%lo", "192.168.1.1", "::ffff:127.0.0.1"}) {
    auto config = options();
    config.address = QString::fromLatin1(address);
    QVERIFY2(!server.start(config, {}, ok, nullptr), address);
  }
  auto config = options();
  config.workerCount = 0;
  QVERIFY(!server.start(config, {}, ok, nullptr));
  config = options();
  config.maxQueuedConnections = 0;
  QVERIFY(!server.start(config, {}, ok, nullptr));
  config = options();
  config.maxBodyBytes = 0;
  QVERIFY(!server.start(config, {}, ok, nullptr));
  config = options();
  config.ioTimeoutSeconds = 0;
  QVERIFY(!server.start(config, {}, ok, nullptr));
  QVERIFY(!server.start(options(), {}, {}, nullptr));
}

void TestHttpServer::ipv6Loopback() {
  HttpServer server;
  auto config = options();
  config.address = QStringLiteral("::1");
  QString error;
  QVERIFY2(server.start(config, {}, ok, &error), qPrintable(error));
  QTcpSocket socket;
  QVERIFY(connectSocket(socket, server.port(), QHostAddress::LocalHostIPv6));
  QVERIFY(sendBytes(socket, "GET / HTTP/1.1\r\nHost: [::1]\r\nConnection: close\r\n\r\n"));
  QCOMPARE(status(receiveClosed(socket)), 200);
}

void TestHttpServer::keepAliveAndHead() {
  AccessRecords records;
  HttpServer server;
  auto config = options();
  config.keepAliveRequests = 2;
  config.accessObserver = [&](const HttpAccessRecord &p_record) { records.append(p_record); };
  QVERIFY(server.start(config, {}, ok, nullptr));
  QTcpSocket socket;
  QVERIFY(connectSocket(socket, server.port()));
  const QByteArray request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
  QVERIFY(sendBytes(socket, request + request + request));
  const auto responses = receiveClosed(socket);
  QCOMPARE(responses.count("HTTP/1.1 200"), 2);
  QCOMPARE(responses.count("complete"), 2);
  QCOMPARE(socket.state(), QAbstractSocket::UnconnectedState);
  const auto head =
      exchange(server.port(), "HEAD / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
  QCOMPARE(status(head), 200);
  QVERIFY(body(head).isEmpty());
  QVERIFY(head.contains("Content-Length: 8\r\n"));
  const auto range = exchange(
      server.port(),
      "GET / HTTP/1.1\r\nHost: localhost\r\nRange: bytes=0-1\r\nConnection: close\r\n\r\n");
  QCOMPARE(status(range), 200);
  QCOMPARE(body(range), QByteArray("complete"));
  server.stop();
  const auto events = records.snapshot();
  QCOMPARE(events.size(), 4);
  QSet<QString> ids;
  for (const auto &event : events) {
    QCOMPARE(event.status, 200);
    QVERIFY(event.responseWritten);
    ids.insert(event.context.requestId);
  }
  QCOMPARE(ids.size(), 4);
  QCOMPARE(events[2].context.method, QByteArray("HEAD"));
}

void TestHttpServer::callbackExceptionsAreContained() {
  HttpServer server;
  QVERIFY(server.start(
      options(),
      [](const HttpRequest &p_request) -> std::optional<HttpResponse> {
        if (p_request.target == "/admission") {
          throw std::runtime_error("private admission detail");
        }
        return std::nullopt;
      },
      [](const HttpRequest &p_request) -> HttpResponse {
        if (p_request.target == "/handler") {
          throw std::runtime_error("private handler detail");
        }
        return ok(p_request);
      },
      nullptr));
  for (const auto path : {"/admission", "/handler"}) {
    const auto response =
        exchange(server.port(), QByteArray("GET ") + path +
                                    " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    QCOMPARE(status(response), 500);
    QVERIFY(!response.contains("private"));
  }
  QCOMPARE(status(exchange(server.port(),
                           "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n")),
           200);
}

void TestHttpServer::accessRecordsCoverRejectedAndPartialRequests() {
  AccessRecords records;
  HttpServer server;
  auto config = options();
  config.accessObserver = [&](const HttpAccessRecord &p_record) { records.append(p_record); };
  QVERIFY(server.start(
      config, {},
      [](const HttpRequest &p_request) {
        if (p_request.target == "/throw") {
          throw std::runtime_error("private detail");
        }
        auto response = ok(p_request);
        response.headers = {{"X-Request-Id", "untrusted"}, {"x-request-id", "duplicate"}};
        return response;
      },
      nullptr));
  const auto success =
      exchange(server.port(), "GET / HTTP/1.1\r\nHost: local\r\nConnection: close\r\n\r\n");
  QCOMPARE(status(success), 200);
  QCOMPARE(success.toLower().count("x-request-id:"), 1);
  QVERIFY(!success.contains("untrusted"));
  QCOMPARE(status(exchange(server.port(), "GET / HTTP/9.9\r\n\r\n")), 400);
  QCOMPARE(status(exchange(server.port(),
                           "GET /throw HTTP/1.1\r\nHost: local\r\nConnection: close\r\n\r\n")),
           500);
  QVERIFY(exchange(server.port(), "GET /unfinished").isEmpty());
  server.stop();
  const auto events = records.snapshot();
  QCOMPARE(events.size(), 4);
  const QList<int> expected{200, 400, 500, 0};
  QSet<QString> ids;
  for (int i = 0; i < events.size(); ++i) {
    QCOMPARE(events[i].status, expected[i]);
    QCOMPARE(events[i].responseWritten, i != 3);
    QCOMPARE(events[i].context.requestId.size(), 32);
    QVERIFY(events[i].durationMs >= 0);
    ids.insert(events[i].context.requestId);
  }
  QCOMPARE(ids.size(), 4);
  QVERIFY(success.contains("X-Request-Id: " + events[0].context.requestId.toLatin1()));
  QVERIFY(events.last().durationMs >= 500);
}

void TestHttpServer::accessRecordsIgnoreIdleEmptyLines() {
  AccessRecords records;
  HttpServer server;
  auto config = options();
  config.accessObserver = [&](const HttpAccessRecord &p_record) { records.append(p_record); };
  QVERIFY(server.start(config, {}, ok, nullptr));
  for (const bool closeEarly : {true, false}) {
    QTcpSocket socket;
    QVERIFY(connectSocket(socket, server.port()));
    QVERIFY(sendBytes(socket, "GET / HTTP/1.1\r\nHost: local\r\n\r\n\r\n"));
    QByteArray response;
    QDeadlineTimer deadline(2000);
    while (!response.endsWith("complete") && !deadline.hasExpired()) {
      socket.waitForReadyRead(static_cast<int>(deadline.remainingTime()));
      response += socket.readAll();
    }
    QCOMPARE(status(response), 200);
    if (closeEarly) {
      socket.disconnectFromHost();
      if (socket.state() != QAbstractSocket::UnconnectedState) {
        QVERIFY(socket.waitForDisconnected(2000));
      }
    } else {
      receiveClosed(socket);
      QCOMPARE(socket.state(), QAbstractSocket::UnconnectedState);
    }
  }
  server.stop();
  const auto events = records.snapshot();
  QCOMPARE(events.size(), 2);
  for (const auto &event : events) {
    QCOMPARE(event.status, 200);
    QVERIFY(event.responseWritten);
  }
}

QTEST_GUILESS_MAIN(TestHttpServer)
#include "test_httpserver.moc"
