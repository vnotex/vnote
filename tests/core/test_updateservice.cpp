// End-to-end unit tests for UpdateService: the release CHECK. That is the whole
// service -- VNote downloads nothing and the user is sent to the release page.
//
// Design notes for anyone extending this file:
//
//   * The REAL QNetworkAccessManager path is exercised against a local HTTP
//     server rather than mocked, so HEAD semantics, redirects, origin policy
//     and cancellation are covered as shipped. The service is pointed at it
//     through the production seams testSetEndpointOverride() /
//     testSetExtraAllowedHost(); the latter is what permits plain HTTP, and ONLY
//     for the named host.
//
//   * Nothing here may write anywhere. The suite asserts that explicitly
//     (testTheCheckWritesNothingToDisk).
//
// Not covered, deliberately: the 60 s request timeout. Reaching it needs real
// wall-clock time and there is no seam to shorten it; the cancellation case
// exercises the same waitForReply() machinery and proves it can be unblocked
// promptly.

#include <QtTest>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QScopedPointer>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <core/services/updateservice.h>

using namespace vnotex;

namespace tests {

const QString c_latestPath = QStringLiteral("/vnotex/vnote/releases/latest");

// --------------------------------------------------------------------------
// Local HTTP server
// --------------------------------------------------------------------------
//
// Deliberately real HTTP over a real socket: the point is to drive the shipped
// QNetworkAccessManager code path, including manual redirect following, the
// allowlist check on EVERY hop, and the 250 ms cancellation poll.
class TestHttpServer : public QTcpServer {
  Q_OBJECT

public:
  struct Response {
    int status = 200;
    QByteArray location; // only for 3xx
    int delayMs = 0;
    // A HEAD response may declare a large GET body, but sends no body. Keep
    // that connection open to prove the client finishes from headers alone.
    qint64 declaredLength = 0;
  };

  explicit TestHttpServer(QObject *p_parent = nullptr) : QTcpServer(p_parent) {}

  bool start() { return listen(QHostAddress::LocalHost, 0); }

  QUrl urlFor(const QString &p_path) const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(p_path));
  }

  void serveStatus(const QString &p_path, int p_status) {
    Response response;
    response.status = p_status;
    m_routes.insert(p_path, response);
  }

  void serveRedirect(const QString &p_path, const QString &p_location, int p_status = 302) {
    Response response;
    response.status = p_status;
    response.location = p_location.toUtf8();
    m_routes.insert(p_path, response);
  }

  void serveHeadOnly(const QString &p_path, const QString &p_location, qint64 p_declaredLength) {
    Response response;
    response.status = 302;
    response.location = p_location.toUtf8();
    response.declaredLength = p_declaredLength;
    m_routes.insert(p_path, response);
  }

  void setDelay(const QString &p_path, int p_delayMs) {
    if (m_routes.contains(p_path)) {
      m_routes[p_path].delayMs = p_delayMs;
    }
  }

  const QStringList &requestedPaths() const { return m_requests; }
  const QStringList &requestedMethods() const { return m_methods; }

signals:
  // Lets a test act the moment a specific request is genuinely in flight,
  // instead of guessing at a delay.
  void requestReceived(const QString &p_path);

protected:
  void incomingConnection(qintptr p_socketDescriptor) override {
    auto *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(p_socketDescriptor)) {
      delete socket;
      return;
    }
    connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() { onReadyRead(socket); });
    // Deliberately touches NOTHING on the server. A socket that is still alive
    // when the server is torn down (the cancellation case leaves one parked on
    // a delayed reply) is destroyed as a child AFTER ~TestHttpServer has run,
    // and QTcpSocket's destructor emits disconnected() on the way out -- so a
    // handler that reached back into a server member would be a use-after-free.
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
  }

private:
  void onReadyRead(QTcpSocket *p_socket) {
    // The pending request lives on the socket itself, for the reason above.
    static const char *const c_bufferProperty = "vnoteRequestBuffer";
    QByteArray buffer = p_socket->property(c_bufferProperty).toByteArray();
    buffer.append(p_socket->readAll());
    if (!buffer.contains("\r\n\r\n")) {
      p_socket->setProperty(c_bufferProperty, buffer);
      return;
    }

    const QByteArray requestLine = buffer.left(buffer.indexOf("\r\n"));
    const QList<QByteArray> parts = requestLine.split(' ');
    const QString path = parts.size() >= 2 ? QString::fromUtf8(parts.at(1)) : QString();
    m_requests.append(path);
    const QString method = QString::fromLatin1(parts.value(0));
    m_methods.append(method);
    emit requestReceived(path);
    // Only one request per connection: every response says Connection: close.
    p_socket->setProperty(c_bufferProperty, QByteArray());

    Response response = m_routes.contains(path) ? m_routes.value(path) : notFoundResponse();
    if (method != QLatin1String("HEAD")) {
      response = Response();
      response.status = 405;
    }

    if (response.delayMs > 0) {
      // Bound to the socket, so a client that aborts (the cancellation case)
      // simply never gets a reply instead of writing through a dead pointer.
      QTimer::singleShot(response.delayMs, p_socket,
                         [p_socket, response]() { writeResponse(p_socket, response); });
      return;
    }
    writeResponse(p_socket, response);
  }

  static Response notFoundResponse() {
    Response response;
    response.status = 404;
    return response;
  }

  static void writeResponse(QTcpSocket *p_socket, const Response &p_response) {
    QByteArray head = "HTTP/1.1 " + QByteArray::number(p_response.status) + " Fixture\r\n";
    if (!p_response.location.isEmpty()) {
      head += "Location: " + p_response.location + "\r\n";
    }
    head += "Content-Length: " + QByteArray::number(p_response.declaredLength) + "\r\n";
    head += "Connection: close\r\n\r\n";

    p_socket->write(head);
    p_socket->flush();
    if (p_response.declaredLength > 0) {
      // No body follows. A HEAD client must finish without waiting for EOF.
      return;
    }
    p_socket->disconnectFromHost();
  }

  QHash<QString, Response> m_routes;
  QStringList m_requests;
  QStringList m_methods;
};

// --------------------------------------------------------------------------
// The test
// --------------------------------------------------------------------------

class TestUpdateService : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void testNewerReleaseIsReported_data();
  void testNewerReleaseIsReported();
  void testOlderOrEqualReleaseOffersNoUpdate();
  void testUnexpectedResponseFails_data();
  void testUnexpectedResponseFails();
  void testInvalidRedirectFails_data();
  void testInvalidRedirectFails();
  void testHeadDoesNotFetchTagOrBody_data();
  void testHeadDoesNotFetchTagOrBody();
  void testRedirectWithinTheAllowlistIsFollowed();
  void testTooManyRedirectsAreRefused();
  void testCancelDuringACheckReturnsPromptly();
  void testASecondCheckIsIgnoredWhileOneIsRunning();
  void testSourceStringRoundTripDefaultsToGitee();
  void testSourceChangeIsIgnoredWhileACheckIsRunning();

  // The load-bearing invariant of the whole feature.
  void testTheCheckWritesNothingToDisk();

private:
  void publishRelease(const QString &p_version, int p_status = 302);

  UpdateService *makeService(const QString &p_currentVersion);

  struct Outcome {
    bool finished = false;
    bool ok = false;
    UpdateInfo info;
    QString error;
  };

  Outcome runCheck(UpdateService *p_service, int p_timeoutMs = 30000);

  QScopedPointer<QTemporaryDir> m_temp;
  TestHttpServer *m_server = nullptr;
  QScopedPointer<UpdateService> m_service;
};

// ---------------------------------------------------------------- fixtures

void TestUpdateService::publishRelease(const QString &p_version, int p_status) {
  m_server->serveRedirect(
      c_latestPath, QStringLiteral("/vnotex/vnote/releases/tag/v%1").arg(p_version), p_status);
}

UpdateService *TestUpdateService::makeService(const QString &p_currentVersion) {
  auto *service = new UpdateService(p_currentVersion);
  service->testSetEndpointOverride(m_server->urlFor(c_latestPath));
  service->testSetExtraAllowedHost(QStringLiteral("127.0.0.1"));
  return service;
}

TestUpdateService::Outcome TestUpdateService::runCheck(UpdateService *p_service, int p_timeoutMs) {
  Outcome outcome;
  QEventLoop loop;

  const auto onFinished = connect(p_service, &UpdateService::checkFinished, &loop,
                                  [&outcome, &loop](const UpdateInfo &p_info) {
                                    outcome.finished = true;
                                    outcome.ok = true;
                                    outcome.info = p_info;
                                    loop.quit();
                                  });
  const auto onFailed =
      connect(p_service, &UpdateService::failed, &loop, [&outcome, &loop](const QString &p_msg) {
        outcome.finished = true;
        outcome.ok = false;
        outcome.error = p_msg;
        loop.quit();
      });

  QTimer::singleShot(p_timeoutMs, &loop, &QEventLoop::quit);
  p_service->checkForUpdates();
  loop.exec();

  QObject::disconnect(onFinished);
  QObject::disconnect(onFailed);
  return outcome;
}

// ------------------------------------------------------------ setup/teardown

void TestUpdateService::init() {
  m_temp.reset(new QTemporaryDir());
  QVERIFY(m_temp->isValid());

  m_server = new TestHttpServer(this);
  QVERIFY2(m_server->start(), qPrintable(m_server->errorString()));
}

void TestUpdateService::cleanup() {
  // The service destructor cancels and WAITS for its worker; it must go first.
  m_service.reset();

  if (m_server) {
    m_server->close();
    delete m_server;
    m_server = nullptr;
  }

  m_temp.reset();
}

// ------------------------------------------------------------------- check

void TestUpdateService::testNewerReleaseIsReported_data() {
  QTest::addColumn<QString>("source");
  QTest::addColumn<int>("status");
  for (const auto &source : {QStringLiteral("github"), QStringLiteral("gitee")}) {
    for (int status : {301, 302, 303, 307, 308}) {
      QTest::newRow(qPrintable(source + QString::number(status))) << source << status;
    }
  }
}

void TestUpdateService::testNewerReleaseIsReported() {
  QFETCH(QString, source);
  QFETCH(int, status);
  publishRelease(QStringLiteral("4.4.3"), status);
  m_service.reset(makeService(QStringLiteral("4.4.2")));
  m_service->setSource(UpdateService::sourceFromString(source));
  const Outcome outcome = runCheck(m_service.data());
  QVERIFY(outcome.finished);
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY(outcome.info.updateAvailable);
  QCOMPARE(outcome.info.latestVersion, QStringLiteral("4.4.3"));
  QCOMPARE(outcome.info.currentVersion, QStringLiteral("4.4.2"));
  QVERIFY(outcome.info.releaseNotes.isEmpty());
  const QString page =
      source == QLatin1String("github")
          ? QStringLiteral("https://github.com/vnotex/vnote/releases/tag/v4.4.3")
          : QStringLiteral("https://gitee.com/vnotex/vnote/releases#release-v4.4.3");
  QCOMPARE(outcome.info.releaseUrl, page);
  QCOMPARE(m_server->requestedPaths(), QStringList{c_latestPath});
  QCOMPARE(m_server->requestedMethods(), QStringList{QStringLiteral("HEAD")});
}
void TestUpdateService::testOlderOrEqualReleaseOffersNoUpdate() {
  publishRelease(QStringLiteral("4.4.2"));

  m_service.reset(makeService(QStringLiteral("4.4.2")));
  Outcome outcome = runCheck(m_service.data());
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY2(!outcome.info.updateAvailable, "an equal version was offered as an update");
  // The release is still described, so a manual check can say "up to date"
  // AND link the page.
  QCOMPARE(outcome.info.latestVersion, QStringLiteral("4.4.2"));

  m_service.reset(makeService(QStringLiteral("4.5.0")));
  outcome = runCheck(m_service.data());
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY2(!outcome.info.updateAvailable, "an older version was offered as an update");
}

void TestUpdateService::testUnexpectedResponseFails_data() {
  QTest::addColumn<int>("status");
  QTest::addColumn<bool>("hasLocation");
  for (int status : {200, 204, 300, 304, 305, 403, 404, 429, 500}) {
    QTest::newRow(qPrintable(QString::number(status))) << status << true;
  }
  QTest::newRow("missing-location") << 302 << false;
}

void TestUpdateService::testUnexpectedResponseFails() {
  QFETCH(int, status);
  QFETCH(bool, hasLocation);
  if (hasLocation) {
    publishRelease(QStringLiteral("4.4.3"), status);
  } else {
    m_server->serveStatus(c_latestPath, status);
  }
  m_service.reset(makeService(QStringLiteral("4.4.2")));
  const Outcome outcome = runCheck(m_service.data());
  QVERIFY(outcome.finished);
  QVERIFY(!outcome.ok);
  QCOMPARE(m_server->requestedPaths(), QStringList{c_latestPath});
}

void TestUpdateService::testInvalidRedirectFails_data() {
  QTest::addColumn<QString>("source");
  QTest::addColumn<QString>("target");
  const QStringList targets{
      QStringLiteral("/other/vnote/releases/tag/v4.4.3"),
      QStringLiteral("/vnotex/other/releases/tag/v4.4.3"),
      QStringLiteral("/vnotex/vnote/releases/download/v4.4.3/VNote.zip"),
      QStringLiteral("/vnotex/vnote/repository/archive/v4.4.3"),
      QStringLiteral("/login"),
      QStringLiteral("/vnotex/vnote/releases/tag/v4.4"),
      QStringLiteral("/vnotex/vnote/releases/tag/v4.4.3.1"),
      QStringLiteral("/vnotex/vnote/releases/tag/v4.4.3-rc1"),
      QStringLiteral("/vnotex/vnote/releases/tag/v4.4.3+build"),
      QStringLiteral("/vnotex/vnote/releases/tag/v9999999999999.1.0"),
      QStringLiteral("/vnotex/vnote/releases/tag/v-4.4.3"),
      QStringLiteral("/vnotex/vnote/releases/tag/v4..3"),
      QStringLiteral("/vnotex/vnote/releases/tag/v"),
      QStringLiteral("/vnotex/vnote/releases/tag/v4.4.3/asset"),
      QStringLiteral("/vnotex/vnote/releases/tag/v4.4.3?download=1"),
      QStringLiteral("/vnotex/vnote/releases/tag/v4.4.3#fragment"),
      QStringLiteral("http://user@127.0.0.1:{port}/vnotex/vnote/releases/tag/v4.4.3"),
      QStringLiteral("http://@127.0.0.1:{port}/vnotex/vnote/releases/tag/v4.4.3"),
      QStringLiteral("http://127.0.0.1:1/vnotex/vnote/releases/tag/v4.4.3"),
      QStringLiteral("https://not-allowed.invalid/vnotex/vnote/releases/tag/v4.4.3"),
      QStringLiteral("https://evil.githubusercontent.com/vnotex/vnote/releases/tag/v4.4.3"),
      QStringLiteral("https://evil.gitee.com/vnotex/vnote/releases/tag/v4.4.3"),
      QStringLiteral("https://api.github.com/repos/vnotex/vnote/releases/latest"),
      QStringLiteral("https://github.com/vnotex/vnote/releases/tag/v4.4.3"),
      QStringLiteral("https://gitee.com/vnotex/vnote/releases/tag/v4.4.3"),
      QStringLiteral("http://github.com/vnotex/vnote/releases/tag/v4.4.3"),
      QStringLiteral("https://127.0.0.1:{port}/vnotex/vnote/releases/tag/v4.4.3")};
  for (const auto &source : {QStringLiteral("github"), QStringLiteral("gitee")}) {
    for (const auto &target : targets) {
      QTest::newRow(qPrintable(source + QLatin1Char(' ') + target)) << source << target;
    }
  }
}

void TestUpdateService::testInvalidRedirectFails() {
  QFETCH(QString, source);
  QFETCH(QString, target);
  target.replace(QStringLiteral("{port}"), QString::number(m_server->serverPort()));
  m_server->serveRedirect(c_latestPath, target);
  m_service.reset(makeService(QStringLiteral("4.4.2")));
  m_service->setSource(UpdateService::sourceFromString(source));
  const Outcome outcome = runCheck(m_service.data());
  QVERIFY(outcome.finished);
  QVERIFY(!outcome.ok);
  QCOMPARE(m_server->requestedPaths(), QStringList{c_latestPath});
}

void TestUpdateService::testHeadDoesNotFetchTagOrBody_data() {
  QTest::addColumn<QString>("source");
  QTest::newRow("github") << QStringLiteral("github");
  QTest::newRow("gitee") << QStringLiteral("gitee");
}

void TestUpdateService::testHeadDoesNotFetchTagOrBody() {
  QFETCH(QString, source);
  const QString tag = QStringLiteral("/vnotex/vnote/releases/tag/v4.4.3");
  m_server->serveHeadOnly(c_latestPath, tag, 8LL * 1024 * 1024 * 1024);
  // Gitee actually redirects its tag page to an archive. Never request that page.
  m_server->serveRedirect(tag, QStringLiteral("/vnotex/vnote/repository/archive/v4.4.3"));
  m_service.reset(makeService(QStringLiteral("4.4.2")));
  m_service->setSource(UpdateService::sourceFromString(source));
  const Outcome outcome = runCheck(m_service.data(), 10000);
  QVERIFY(outcome.finished);
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QCOMPARE(outcome.info.latestVersion, QStringLiteral("4.4.3"));
  QCOMPARE(m_server->requestedPaths(), QStringList{c_latestPath});
  QCOMPARE(m_server->requestedMethods(), QStringList{QStringLiteral("HEAD")});
}

// --------------------------------------------------------------- networking

void TestUpdateService::testRedirectWithinTheAllowlistIsFollowed() {
  m_server->serveRedirect(c_latestPath, QStringLiteral("latest/"));
  m_server->serveRedirect(c_latestPath + QLatin1Char('/'), QStringLiteral("../tag/v4.4.3"));
  m_service.reset(makeService(QStringLiteral("4.4.2")));
  const Outcome outcome = runCheck(m_service.data());
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QCOMPARE(outcome.info.latestVersion, QStringLiteral("4.4.3"));
  QCOMPARE(m_server->requestedPaths(),
           (QStringList{c_latestPath, c_latestPath + QLatin1Char('/')}));
}

void TestUpdateService::testTooManyRedirectsAreRefused() {
  m_server->serveRedirect(c_latestPath, c_latestPath);
  m_service.reset(makeService(QStringLiteral("4.4.2")));
  const Outcome outcome = runCheck(m_service.data());
  QVERIFY(outcome.finished);
  QVERIFY(!outcome.ok);
  QCOMPARE(m_server->requestedPaths().size(), UpdateService::c_maxRedirects + 1);
}

// Cancellation is POLLED every 250 ms inside the blocking request, so it must
// return in well under the 60 s request timeout. Without the poll, quitting
// VNote during a stalled check would block service teardown -- and therefore
// vxcore_context_destroy -- for the whole timeout.
//
// cancel() is fired from the server's own requestReceived signal rather than
// after a fixed delay, so the request is provably IN FLIGHT when it happens.
void TestUpdateService::testCancelDuringACheckReturnsPromptly() {
  publishRelease(QStringLiteral("4.4.3"));
  m_server->setDelay(c_latestPath, 120000);

  m_service.reset(makeService(QStringLiteral("4.4.2")));

  Outcome outcome;
  QEventLoop loop;
  const auto onFinished = connect(m_service.data(), &UpdateService::checkFinished, &loop,
                                  [&outcome, &loop](const UpdateInfo &) {
                                    outcome.finished = true;
                                    outcome.ok = true;
                                    loop.quit();
                                  });
  const auto onFailed = connect(m_service.data(), &UpdateService::failed, &loop,
                                [&outcome, &loop](const QString &p_msg) {
                                  outcome.finished = true;
                                  outcome.ok = false;
                                  outcome.error = p_msg;
                                  loop.quit();
                                });

  QElapsedTimer sinceRequest;
  bool requestSeen = false;
  const auto onRequest = connect(m_server, &TestHttpServer::requestReceived, &loop,
                                 [this, &sinceRequest, &requestSeen](const QString &p_path) {
                                   if (requestSeen || p_path != c_latestPath) {
                                     return;
                                   }
                                   requestSeen = true;
                                   sinceRequest.start();
                                   m_service->cancel();
                                 });

  m_service->checkForUpdates();
  QTimer::singleShot(20000, &loop, &QEventLoop::quit);
  loop.exec();

  QObject::disconnect(onFinished);
  QObject::disconnect(onFailed);
  QObject::disconnect(onRequest);

  QVERIFY2(requestSeen, "the check never reached the server");
  QVERIFY2(outcome.finished, "cancel() did not unblock the check");
  QVERIFY2(!outcome.ok, "a cancelled check reported success");
  QVERIFY2(sinceRequest.elapsed() < 10000,
           qPrintable(QStringLiteral("cancellation took %1 ms after the request went out")
                          .arg(sinceRequest.elapsed())));
}

// A second check while one is running is DROPPED, not queued: two concurrent
// workers would both write a terminal signal and the caller could not tell
// which result belonged to which request.
//
// The RETURN VALUE is the load-bearing part. UpdateController sets the
// manual-vs-startup mode from it; if a dropped request looked accepted, a
// manual click during a background check would re-label the background check's
// outcome and turn a silent failure into a modal warning box.
void TestUpdateService::testASecondCheckIsIgnoredWhileOneIsRunning() {
  publishRelease(QStringLiteral("4.4.3"));
  m_server->setDelay(c_latestPath, 2000);

  m_service.reset(makeService(QStringLiteral("4.4.2")));

  QSignalSpy finished(m_service.data(), &UpdateService::checkFinished);
  QSignalSpy failures(m_service.data(), &UpdateService::failed);

  QVERIFY2(m_service->checkForUpdates(), "the first check was not accepted");
  QTest::qWait(300);
  QVERIFY2(!m_service->checkForUpdates(), "a concurrent check reported itself accepted");
  QVERIFY2(!m_service->checkForUpdates(), "a concurrent check reported itself accepted");

  QVERIFY(finished.wait(20000));
  QTest::qWait(500);

  QCOMPARE(finished.size(), 1);
  QCOMPARE(failures.size(), 0);
  // Exactly one request went out.
  QCOMPARE(m_server->requestedPaths().count(c_latestPath), 1);

  // Accepted again once the previous one is done.
  QVERIFY(m_service->checkForUpdates());
}

// ------------------------------------------------------------ release source

void TestUpdateService::testSourceStringRoundTripDefaultsToGitee() {
  QCOMPARE(UpdateService::sourceFromString(QStringLiteral("github")),
           UpdateService::Source::GitHub);
  QCOMPARE(UpdateService::sourceFromString(QStringLiteral("GiTHub")),
           UpdateService::Source::GitHub);
  QCOMPARE(UpdateService::sourceFromString(QStringLiteral("  github  ")),
           UpdateService::Source::GitHub);
  QCOMPARE(UpdateService::sourceFromString(QStringLiteral("gitee")), UpdateService::Source::Gitee);
  // Only an EXPLICIT "github" selects GitHub; everything else is Gitee.
  QCOMPARE(UpdateService::sourceFromString(QStringLiteral("gitlab")), UpdateService::Source::Gitee);
  QCOMPARE(UpdateService::sourceFromString(QString()), UpdateService::Source::Gitee);

  QCOMPARE(UpdateService::sourceToString(UpdateService::Source::Gitee), QStringLiteral("gitee"));
  QCOMPARE(UpdateService::sourceToString(UpdateService::Source::GitHub), QStringLiteral("github"));

  m_service.reset(makeService(QStringLiteral("4.4.2")));
  QCOMPARE(m_service->source(), UpdateService::Source::Gitee);
  m_service->setSource(UpdateService::Source::GitHub);
  QCOMPARE(m_service->source(), UpdateService::Source::GitHub);
}

// Switching origins mid-flight would let one check send its request to one forge
// and parse the answer as the other's, so it is refused (and logged).
void TestUpdateService::testSourceChangeIsIgnoredWhileACheckIsRunning() {
  publishRelease(QStringLiteral("4.4.3"));
  m_server->setDelay(c_latestPath, 2000);

  m_service.reset(makeService(QStringLiteral("4.4.2")));
  QCOMPARE(m_service->source(), UpdateService::Source::Gitee);

  QSignalSpy finished(m_service.data(), &UpdateService::checkFinished);
  m_service->checkForUpdates();
  QTest::qWait(300);

  m_service->setSource(UpdateService::Source::GitHub);
  QCOMPARE(m_service->source(), UpdateService::Source::Gitee);

  QVERIFY(finished.wait(20000));

  // Accepted again once the check is done.
  m_service->setSource(UpdateService::Source::GitHub);
  QCOMPARE(m_service->source(), UpdateService::Source::GitHub);
}

// ------------------------------------------------------------- the invariant

// VNote never modifies its own install directory, and the update check writes
// nothing at all: no staging tree, no lease, no download. The process working
// directory is redirected into a scratch tree for the duration, so a regression
// that wrote to a RELATIVE path (the classic accident) is caught too, and the
// user's real Downloads folder is snapshotted as the place a resurrected
// downloader would most plausibly target.
//
// The snapshot records size and mtime alongside the path, not the path alone:
// otherwise an implementation that OVERWROTE an existing file in place would
// leave both listings identical and pass.
void TestUpdateService::testTheCheckWritesNothingToDisk() {
  publishRelease(QStringLiteral("4.4.3"));

  const auto snapshot = [](const QString &p_root) {
    QStringList out;
    if (p_root.isEmpty()) {
      return out;
    }
    QDirIterator it(p_root, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QFileInfo info(it.next());
      out.append(QStringLiteral("%1|%2|%3|%4")
                     .arg(QDir(p_root).relativeFilePath(info.absoluteFilePath()))
                     .arg(info.isDir() ? QStringLiteral("d") : QStringLiteral("f"))
                     .arg(info.isDir() ? -1 : info.size())
                     .arg(info.lastModified().toMSecsSinceEpoch()));
    }
    out.sort();
    return out;
  };

  const QString scratch = m_temp->path();
  const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);

  // A pre-existing file in the scratch tree, so "overwritten in place" is
  // actually representable rather than only theoretical.
  {
    QFile sentinel(scratch + QStringLiteral("/pre-existing.txt"));
    QVERIFY(sentinel.open(QIODevice::WriteOnly));
    sentinel.write(QByteArrayLiteral("untouched"));
  }

  const QString previousCwd = QDir::currentPath();
  QVERIFY2(QDir::setCurrent(scratch), "could not redirect the working directory");

  const QStringList scratchBefore = snapshot(scratch);
  const QStringList downloadsBefore = snapshot(downloads);

  m_service.reset(makeService(QStringLiteral("4.4.2")));
  const bool ok = runCheck(m_service.data()).ok;
  QTest::qWait(500);

  const QStringList scratchAfter = snapshot(scratch);
  const QStringList downloadsAfter = snapshot(downloads);
  QDir::setCurrent(previousCwd);

  QVERIFY(ok);
  QCOMPARE(scratchAfter, scratchBefore);
  QCOMPARE(downloadsAfter, downloadsBefore);

  for (const QString &path : m_server->requestedPaths()) {
    QVERIFY2(!path.contains(QStringLiteral("manifest")), qPrintable(path));
    QVERIFY2(!path.contains(QStringLiteral("minisig")), qPrintable(path));
    QVERIFY2(!path.startsWith(QStringLiteral("/download/")), qPrintable(path));
  }
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestUpdateService)
#include "test_updateservice.moc"
