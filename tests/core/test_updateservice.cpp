// End-to-end unit tests for UpdateService: the release CHECK. That is the whole
// service -- VNote downloads nothing and the user is sent to the release page.
//
// Design notes for anyone extending this file:
//
//   * The REAL QNetworkAccessManager path is exercised against a local HTTP
//     server rather than mocked, so redirects, the host allowlist, the response
//     cap and cancellation are covered as shipped. The service is pointed at it
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
    QByteArray body;
    QByteArray location; // only for 3xx
    int delayMs = 0;
    // When > 0 the header declares THIS length while only `body` is written and
    // the socket is left open, so a client that waits for the whole body hangs
    // instead of failing fast. Used to prove the response cap ABORTS.
    qint64 declaredLength = 0;
  };

  explicit TestHttpServer(QObject *p_parent = nullptr) : QTcpServer(p_parent) {}

  bool start() { return listen(QHostAddress::LocalHost, 0); }

  QUrl urlFor(const QString &p_path) const {
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(p_path));
  }

  void serve(const QString &p_path, const QByteArray &p_body) {
    Response response;
    response.body = p_body;
    m_routes.insert(p_path, response);
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

  // Writes p_body but declares p_declaredLength and never closes the socket.
  void serveNeverEnding(const QString &p_path, const QByteArray &p_body, qint64 p_declaredLength) {
    Response response;
    response.body = p_body;
    response.declaredLength = p_declaredLength;
    m_routes.insert(p_path, response);
  }

  void setDelay(const QString &p_path, int p_delayMs) {
    if (m_routes.contains(p_path)) {
      m_routes[p_path].delayMs = p_delayMs;
    }
  }

  QStringList requestedPaths() const { return m_requests; }
  void clearRequests() { m_requests.clear(); }

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
    emit requestReceived(path);
    // Only one request per connection: every response says Connection: close.
    p_socket->setProperty(c_bufferProperty, QByteArray());

    const Response response = m_routes.contains(path) ? m_routes.value(path) : notFoundResponse();

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

  static QByteArray statusText(int p_status) {
    switch (p_status) {
    case 200:
      return QByteArrayLiteral("200 OK");
    case 301:
      return QByteArrayLiteral("301 Moved Permanently");
    case 302:
      return QByteArrayLiteral("302 Found");
    case 404:
      return QByteArrayLiteral("404 Not Found");
    default:
      return QByteArrayLiteral("500 Internal Server Error");
    }
  }

  static void writeResponse(QTcpSocket *p_socket, const Response &p_response) {
    const qint64 declared =
        p_response.declaredLength > 0 ? p_response.declaredLength : p_response.body.size();

    QByteArray head = "HTTP/1.1 " + statusText(p_response.status) + "\r\n";
    if (!p_response.location.isEmpty()) {
      head += "Location: " + p_response.location + "\r\n";
    }
    head += "Content-Type: application/json\r\n";
    head += "Content-Length: " + QByteArray::number(declared) + "\r\n";
    head += "Connection: close\r\n\r\n";

    p_socket->write(head);
    if (!p_response.body.isEmpty()) {
      p_socket->write(p_response.body);
    }
    p_socket->flush();
    if (p_response.declaredLength > 0) {
      // Deliberately leave the socket open: the client must not be able to
      // finish this reply normally.
      return;
    }
    p_socket->disconnectFromHost();
  }

  QHash<QString, Response> m_routes;
  QStringList m_requests;
};

// --------------------------------------------------------------------------
// The test
// --------------------------------------------------------------------------

class TestUpdateService : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  // Check outcomes.
  void testNewerReleaseIsReported();
  void testOlderOrEqualReleaseOffersNoUpdate();
  void testUnidentifiableReleaseIsAFailure();
  void testAssetsAreIgnoredEntirely();

  // Networking.
  void testRedirectWithinTheAllowlistIsFollowed();
  void testRedirectToAnUnexpectedHostIsRefused();
  void testRedirectDowngradingToPlainHttpIsRefused();
  void testTooManyRedirectsAreRefused();
  void testOversizedApiResponseAbortsTheReply();
  void testCancelDuringACheckReturnsPromptly();
  void testASecondCheckIsIgnoredWhileOneIsRunning();

  // Release source.
  void testSourceStringRoundTripDefaultsToGitee();
  void testAllowlistIsPerSource();
  void testGiteeRedirectToGitHubHostsIsRefused();
  void testGitHubRedirectToGiteeHostsIsRefused();
  void testReleasesPageUrlFollowsTheSource();
  void testGiteeSynthesizesTheReleasePageUrl();
  void testUnusableGitHubReleasePageUrlFallsBackToATagUrl();
  void testSourceChangeIsIgnoredWhileACheckIsRunning();

  // The load-bearing invariant of the whole feature.
  void testTheCheckWritesNothingToDisk();

private:
  // Serves a release object at /api/latest. `p_withAssets` adds an assets[]
  // array, which the service must ignore completely.
  void publishRelease(const QString &p_version, bool p_withAssets = false);

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

void TestUpdateService::publishRelease(const QString &p_version, bool p_withAssets) {
  QJsonObject release;
  release[QStringLiteral("tag_name")] = QStringLiteral("v%1").arg(p_version);
  release[QStringLiteral("body")] = QStringLiteral("Release notes for %1").arg(p_version);
  release[QStringLiteral("html_url")] =
      QStringLiteral("https://github.com/vnotex/vnote/releases/tag/v%1").arg(p_version);

  if (p_withAssets) {
    QJsonObject asset;
    asset[QStringLiteral("name")] = QStringLiteral("VNote-%1-win64.zip").arg(p_version);
    asset[QStringLiteral("browser_download_url")] =
        m_server->urlFor(QStringLiteral("/download/VNote.zip")).toString();
    asset[QStringLiteral("size")] = 4096;
    QJsonArray assets;
    assets.append(asset);
    release[QStringLiteral("assets")] = assets;

    m_server->serve(QStringLiteral("/download/VNote.zip"), QByteArray(4096, 'z'));
  }

  m_server->serve(QStringLiteral("/api/latest"),
                  QJsonDocument(release).toJson(QJsonDocument::Compact));
}

UpdateService *TestUpdateService::makeService(const QString &p_currentVersion) {
  auto *service = new UpdateService(p_currentVersion);
  service->testSetEndpointOverride(m_server->urlFor(QStringLiteral("/api/latest")));
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

void TestUpdateService::testNewerReleaseIsReported() {
  publishRelease(QStringLiteral("4.4.3"));

  m_service.reset(makeService(QStringLiteral("4.4.2")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY2(outcome.finished, "the check never completed");
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY(outcome.info.updateAvailable);
  QCOMPARE(outcome.info.latestVersion, QStringLiteral("4.4.3"));
  QCOMPARE(outcome.info.currentVersion, QStringLiteral("4.4.2"));
  QCOMPARE(outcome.info.releaseNotes, QStringLiteral("Release notes for 4.4.3"));
  // The default source is Gitee, which publishes no html_url, so the page URL is
  // synthesized from the tag. testGiteeSynthesizesTheReleasePageUrl() covers
  // both sources explicitly.
  QCOMPARE(outcome.info.releaseUrl,
           QStringLiteral("https://gitee.com/vnotex/vnote/releases/tag/v4.4.3"));
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

void TestUpdateService::testUnidentifiableReleaseIsAFailure() {
  // A body that parses but carries no tag_name.
  m_server->serve(QStringLiteral("/api/latest"), QByteArrayLiteral("{\"body\":\"nothing\"}"));

  m_service.reset(makeService(QStringLiteral("4.4.2")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(!outcome.ok, "a release with no tag was reported as a successful check");
  QVERIFY2(outcome.error.contains(QStringLiteral("could not be identified")),
           qPrintable(outcome.error));
}

// VNote downloads nothing, so assets[] must be ignored outright -- no asset is
// selected, and above all no asset URL is ever requested.
void TestUpdateService::testAssetsAreIgnoredEntirely() {
  publishRelease(QStringLiteral("4.4.3"), /*p_withAssets=*/true);

  m_service.reset(makeService(QStringLiteral("4.4.2")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY(outcome.info.updateAvailable);

  // Give a hypothetical download every chance to start before asserting.
  QTest::qWait(500);
  for (const QString &path : m_server->requestedPaths()) {
    QVERIFY2(!path.startsWith(QStringLiteral("/download/")),
             qPrintable(QStringLiteral("the check fetched an asset: %1").arg(path)));
  }
  QCOMPARE(m_server->requestedPaths(), QStringList{QStringLiteral("/api/latest")});
}

// --------------------------------------------------------------- networking

void TestUpdateService::testRedirectWithinTheAllowlistIsFollowed() {
  publishRelease(QStringLiteral("4.4.3"));
  m_server->serveRedirect(QStringLiteral("/api/latest-redirect"), QStringLiteral("/api/latest"));

  m_service.reset(new UpdateService(QStringLiteral("4.4.2")));
  m_service->testSetEndpointOverride(m_server->urlFor(QStringLiteral("/api/latest-redirect")));
  m_service->testSetExtraAllowedHost(QStringLiteral("127.0.0.1"));

  const Outcome outcome = runCheck(m_service.data());
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QCOMPARE(outcome.info.latestVersion, QStringLiteral("4.4.3"));
  QVERIFY2(m_server->requestedPaths().contains(QStringLiteral("/api/latest")),
           "the redirect was not followed");
}

// Every hop is checked, not just the first URL: a redirect is an attacker's
// cheapest way to move a request somewhere else.
void TestUpdateService::testRedirectToAnUnexpectedHostIsRefused() {
  m_server->serveRedirect(QStringLiteral("/api/latest"),
                          QStringLiteral("http://not-allowed.invalid/releases/latest"));

  m_service.reset(makeService(QStringLiteral("4.4.2")));
  const Outcome outcome = runCheck(m_service.data());

  QVERIFY(outcome.finished);
  QVERIFY2(!outcome.ok, "a redirect to an unexpected host was followed");
  QVERIFY2(outcome.error.contains(QStringLiteral("unexpected host")), qPrintable(outcome.error));
  QVERIFY2(!m_server->requestedPaths().contains(QStringLiteral("/releases/latest")),
           "the redirect target was contacted");
}

// Isolates the SCHEME rule from the HOST rule: api.github.com IS allowlisted, so
// only the no-downgrade predicate can reject this. Nothing is ever sent there --
// the check happens before the request.
void TestUpdateService::testRedirectDowngradingToPlainHttpIsRefused() {
  m_server->serveRedirect(
      QStringLiteral("/api/latest"),
      QStringLiteral("http://api.github.com/repos/vnotex/vnote/releases/latest"));

  m_service.reset(makeService(QStringLiteral("4.4.2")));
  m_service->setSource(UpdateService::Source::GitHub);

  const Outcome outcome = runCheck(m_service.data());
  QVERIFY(outcome.finished);
  QVERIFY2(!outcome.ok, "an HTTPS -> HTTP downgrade was followed");
  QVERIFY2(outcome.error.contains(QStringLiteral("unexpected host")), qPrintable(outcome.error));
}

void TestUpdateService::testTooManyRedirectsAreRefused() {
  const int hops = UpdateService::c_maxRedirects + 2;
  for (int i = 0; i < hops; ++i) {
    m_server->serveRedirect(QStringLiteral("/hop%1").arg(i), QStringLiteral("/hop%1").arg(i + 1));
  }
  m_server->serve(QStringLiteral("/hop%1").arg(hops), QByteArrayLiteral("{}"));

  m_service.reset(new UpdateService(QStringLiteral("4.4.2")));
  m_service->testSetEndpointOverride(m_server->urlFor(QStringLiteral("/hop0")));
  m_service->testSetExtraAllowedHost(QStringLiteral("127.0.0.1"));

  const Outcome outcome = runCheck(m_service.data());
  QVERIFY(outcome.finished);
  QVERIFY2(!outcome.ok, "an unbounded redirect chain was followed");
  QVERIFY2(outcome.error.contains(QStringLiteral("Too many redirects")), qPrintable(outcome.error));
}

// The response cap must ABORT the reply, not buffer everything and reject
// afterwards. The route below declares a gigantic Content-Length and never
// closes the socket, so a client that waits for the whole body would hang until
// the 60 s request timeout; failing inside a few seconds is only possible if the
// cap aborted mid-stream.
void TestUpdateService::testOversizedApiResponseAbortsTheReply() {
  const QByteArray oversized(static_cast<int>(UpdateService::c_maxApiResponseBytes + 1024), 'x');
  m_server->serveNeverEnding(QStringLiteral("/api/latest"), oversized, 8LL * 1024 * 1024 * 1024);

  m_service.reset(makeService(QStringLiteral("4.4.2")));

  QElapsedTimer elapsed;
  elapsed.start();
  const Outcome outcome = runCheck(m_service.data(), 40000);

  QVERIFY2(outcome.finished, "the oversized response never terminated the check");
  QVERIFY2(!outcome.ok, "an oversized API response was accepted");
  QVERIFY2(outcome.error.contains(QStringLiteral("unexpectedly large")), qPrintable(outcome.error));
  QVERIFY2(elapsed.elapsed() < 30000,
           qPrintable(QStringLiteral("the cap took %1 ms, so it did not abort mid-stream")
                          .arg(elapsed.elapsed())));
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
  m_server->setDelay(QStringLiteral("/api/latest"), 120000);

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
                                   if (requestSeen || p_path != QStringLiteral("/api/latest")) {
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
  QVERIFY2(outcome.error.contains(QStringLiteral("Cancelled")), qPrintable(outcome.error));
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
  m_server->setDelay(QStringLiteral("/api/latest"), 2000);

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
  QCOMPARE(m_server->requestedPaths().count(QStringLiteral("/api/latest")), 1);

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

// The two allowlists are disjoint: a client pointed at one forge must not even
// know the other forge's host names.
void TestUpdateService::testAllowlistIsPerSource() {
  const QStringList github = UpdateService::allowedHosts(UpdateService::Source::GitHub);
  const QStringList gitee = UpdateService::allowedHosts(UpdateService::Source::Gitee);

  QVERIFY(github.contains(QStringLiteral("api.github.com")));
  QVERIFY(github.contains(QStringLiteral("github.com")));
  QVERIFY(!github.contains(QStringLiteral("gitee.com")));

  QVERIFY(gitee.contains(QStringLiteral("gitee.com")));
  QVERIFY(!gitee.contains(QStringLiteral("github.com")));
  QVERIFY(!gitee.contains(QStringLiteral("api.github.com")));
}

void TestUpdateService::testGiteeRedirectToGitHubHostsIsRefused() {
  m_server->serveRedirect(
      QStringLiteral("/api/latest"),
      QStringLiteral("https://api.github.com/repos/vnotex/vnote/releases/latest"));

  m_service.reset(makeService(QStringLiteral("4.4.2")));
  m_service->setSource(UpdateService::Source::Gitee);

  const Outcome outcome = runCheck(m_service.data());
  QVERIFY2(!outcome.ok, "a Gitee client followed a redirect to a GitHub host");
  QVERIFY2(outcome.error.contains(QStringLiteral("unexpected host")), qPrintable(outcome.error));
}

void TestUpdateService::testGitHubRedirectToGiteeHostsIsRefused() {
  m_server->serveRedirect(QStringLiteral("/api/latest"),
                          QStringLiteral("https://gitee.com/vnotex/vnote/releases/latest"));

  m_service.reset(makeService(QStringLiteral("4.4.2")));
  m_service->setSource(UpdateService::Source::GitHub);

  const Outcome outcome = runCheck(m_service.data());
  QVERIFY2(!outcome.ok, "a GitHub client followed a redirect to a Gitee host");
  QVERIFY2(outcome.error.contains(QStringLiteral("unexpected host")), qPrintable(outcome.error));
}

void TestUpdateService::testReleasesPageUrlFollowsTheSource() {
  m_service.reset(makeService(QStringLiteral("4.4.2")));

  QCOMPARE(m_service->releasesPageUrl().toString(),
           QStringLiteral("https://gitee.com/vnotex/vnote/releases"));

  m_service->setSource(UpdateService::Source::GitHub);
  QCOMPARE(m_service->releasesPageUrl().toString(),
           QStringLiteral("https://github.com/vnotex/vnote/releases"));
}

// Gitee's release JSON carries no html_url, so the page URL has to be
// synthesized from the tag. The API fixture here DOES serve an html_url; a
// Gitee client must ignore it rather than send the user to github.com.
//
// The exact shape is pinned on purpose: it was VERIFIED against the live
// https://gitee.com/vnotex/vnote/releases/tag/v4.3.0 page. Note the `tag/`
// segment -- the asset download path does not have it.
void TestUpdateService::testGiteeSynthesizesTheReleasePageUrl() {
  publishRelease(QStringLiteral("4.4.3"));

  m_service.reset(makeService(QStringLiteral("4.4.2")));
  m_service->setSource(UpdateService::Source::Gitee);

  Outcome outcome = runCheck(m_service.data());
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QCOMPARE(outcome.info.releaseUrl,
           QStringLiteral("https://gitee.com/vnotex/vnote/releases/tag/v4.4.3"));

  // The GitHub path keeps using the API's own html_url.
  m_service.reset(makeService(QStringLiteral("4.4.2")));
  m_service->setSource(UpdateService::Source::GitHub);
  outcome = runCheck(m_service.data());
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QCOMPARE(outcome.info.releaseUrl,
           QStringLiteral("https://github.com/vnotex/vnote/releases/tag/v4.4.3"));
}

// Switching origins mid-flight would let one check send its request to one forge
// and parse the answer as the other's, so it is refused (and logged).
void TestUpdateService::testSourceChangeIsIgnoredWhileACheckIsRunning() {
  publishRelease(QStringLiteral("4.4.3"));
  m_server->setDelay(QStringLiteral("/api/latest"), 2000);

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
  publishRelease(QStringLiteral("4.4.3"), /*p_withAssets=*/true);

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

// A GitHub release whose html_url is missing, empty or off-forge must not
// produce a dead (or hostile) "Open Release Page" button: UpdateDialog opens
// info.releaseUrl directly and has no empty-URL branch.
void TestUpdateService::testUnusableGitHubReleasePageUrlFallsBackToATagUrl() {
  const auto publishWithHtmlUrl = [this](const QJsonValue &p_htmlUrl) {
    QJsonObject release;
    release[QStringLiteral("tag_name")] = QStringLiteral("v4.4.3");
    release[QStringLiteral("body")] = QStringLiteral("notes");
    release[QStringLiteral("html_url")] = p_htmlUrl;
    m_server->serve(QStringLiteral("/api/latest"),
                    QJsonDocument(release).toJson(QJsonDocument::Compact));
  };

  const QVector<QPair<QString, QJsonValue>> cases{
      {QStringLiteral("absent"), QJsonValue(QString())},
      {QStringLiteral("not a url"), QJsonValue(QStringLiteral("not a url at all"))},
      {QStringLiteral("foreign host"),
       QJsonValue(QStringLiteral("https://evil.invalid/vnote/releases/tag/v4.4.3"))},
      {QStringLiteral("plain http"),
       QJsonValue(QStringLiteral("http://github.com/vnotex/vnote/releases/tag/v4.4.3"))},
  };

  for (const auto &entry : cases) {
    publishWithHtmlUrl(entry.second);

    m_service.reset(makeService(QStringLiteral("4.4.2")));
    m_service->setSource(UpdateService::Source::GitHub);

    const Outcome outcome = runCheck(m_service.data());
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(outcome.info.releaseUrl,
             QStringLiteral("https://github.com/vnotex/vnote/releases/tag/v4.4.3"));
  }
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestUpdateService)
#include "test_updateservice.moc"
