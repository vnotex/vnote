// End-to-end unit tests for UpdateService: the release CHECK. That is the whole
// service -- only release metadata is fetched; artifacts stay on the release page.
//
// Design notes for anyone extending this file:
//
//   * The REAL QNetworkAccessManager path is exercised against a local HTTP
//     server rather than mocked, so discovery, optional notes, redirects, origin policy
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

const QString c_notesPath = QStringLiteral("/vnotex/vnote/releases");
const QString c_feedPath = QStringLiteral("/vnotex/vnote/releases.atom");

QByteArray githubFeed(const QString &p_version, const QString &p_html) {
  return QStringLiteral(
             "<feed xmlns='http://www.w3.org/2005/Atom'>"
             "<entry><link rel='alternate' href='https://github.com/vnotex/vnote/releases/tag/"
             "continuous-build'/><content type='html'>Wrong release</content></entry>"
             "<entry><content type='html'>%1</content><link rel='alternate' "
             "href='https://github.com/vnotex/vnote/releases/tag/v%2'/></entry></feed>")
      .arg(p_html.toHtmlEscaped(), p_version)
      .toUtf8();
}

QByteArray giteePage(const QString &p_version, const QString &p_markdown) {
  return QStringLiteral(
             "<a data-tag='v%1' href='#release-v%1'>Sidebar</a>"
             "<div class='release-tag-item' data-tag='v0.0.1'>"
             "<textarea class='content'>Wrong release</textarea></div>"
             "<div data-tag=\"v%1\" class=\"release-tag-item\">"
             "<rich-markdown-renderer>&#x000A;<textarea style='display:none;' class='content'>"
             "%2</textarea></rich-markdown-renderer></div>")
      .arg(p_version,
           p_markdown.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("&#x000A;")))
      .toUtf8();
}

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
    QByteArray body;
    bool chunked = false;
    bool keepOpen = false;
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

  void serveBody(const QString &p_path, const QByteArray &p_body, bool p_chunked = false,
                 bool p_keepOpen = false) {
    Response response;
    response.body = p_body;
    response.chunked = p_chunked;
    response.keepOpen = p_keepOpen;
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
    if (method != QLatin1String("HEAD") && method != QLatin1String("GET")) {
      response = Response();
      response.status = 405;
    }

    if (response.delayMs > 0) {
      // Bound to the socket, so a client that aborts (the cancellation case)
      // simply never gets a reply instead of writing through a dead pointer.
      QTimer::singleShot(response.delayMs, p_socket, [p_socket, response, method]() {
        writeResponse(p_socket, response, method);
      });
      return;
    }
    writeResponse(p_socket, response, method);
  }

  static Response notFoundResponse() {
    Response response;
    response.status = 404;
    return response;
  }

  static void writeResponse(QTcpSocket *p_socket, const Response &p_response,
                            const QString &p_method) {
    QByteArray head = "HTTP/1.1 " + QByteArray::number(p_response.status) + " Fixture\r\n";
    if (!p_response.location.isEmpty()) {
      head += "Location: " + p_response.location + "\r\n";
    }
    if (p_response.chunked) {
      head += "Transfer-Encoding: chunked\r\n";
    } else {
      head += "Content-Length: " +
              QByteArray::number(p_method == QLatin1String("HEAD") ? p_response.declaredLength
                                                                   : p_response.body.size()) +
              "\r\n";
    }
    head += "Connection: close\r\n\r\n";

    p_socket->write(head);
    if (p_method == QLatin1String("GET")) {
      if (p_response.chunked) {
        if (!p_response.body.isEmpty()) {
          p_socket->write(QByteArray::number(p_response.body.size(), 16) + "\r\n");
          p_socket->write(p_response.body);
          p_socket->write("\r\n");
        }
        if (!p_response.keepOpen) {
          p_socket->write("0\r\n\r\n");
        }
      } else {
        p_socket->write(p_response.body);
      }
    }
    p_socket->flush();
    if (p_response.keepOpen ||
        (p_method == QLatin1String("HEAD") && p_response.declaredLength > 0)) {
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
  void testOlderOrEqualReleaseOffersNoUpdate_data();
  void testOlderOrEqualReleaseOffersNoUpdate();
  void testReleaseNotes_data();
  void testReleaseNotes();
  void testUnavailableNotesPreserveUpdate_data();
  void testUnavailableNotesPreserveUpdate();
  void testOversizedNotesAreAborted();
  void testNotesTimeoutPreservesUpdate();
  void testNotesRedirectLoopIsBounded();
  void testNotesRedirectPolicy_data();
  void testNotesRedirectPolicy();
  void testUnexpectedResponseFails_data();
  void testUnexpectedResponseFails();
  void testInvalidRedirectFails_data();
  void testInvalidRedirectFails();
  void testDiscoveryDoesNotFollowTagOrReadBody_data();
  void testDiscoveryDoesNotFollowTagOrReadBody();
  void testRedirectWithinTheAllowlistIsFollowed();
  void testTooManyRedirectsAreRefused();
  void testCancelDuringACheckReturnsPromptly_data();
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
  QCOMPARE(
      m_server->requestedPaths(),
      (QStringList{c_latestPath, source == QLatin1String("github") ? c_feedPath : c_notesPath}));
  QCOMPARE(m_server->requestedMethods(),
           (QStringList{QStringLiteral("HEAD"), QStringLiteral("GET")}));
}
void TestUpdateService::testOlderOrEqualReleaseOffersNoUpdate_data() {
  QTest::addColumn<QString>("installed");
  QTest::newRow("equal") << QStringLiteral("4.4.2");
  QTest::newRow("older") << QStringLiteral("4.5.0");
}

void TestUpdateService::testOlderOrEqualReleaseOffersNoUpdate() {
  publishRelease(QStringLiteral("4.4.2"));

  QFETCH(QString, installed);
  m_service.reset(makeService(installed));
  const Outcome outcome = runCheck(m_service.data());
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY(!outcome.info.updateAvailable);
  QCOMPARE(outcome.info.latestVersion, QStringLiteral("4.4.2"));
  QVERIFY(outcome.info.releaseNotes.isEmpty());
  QCOMPARE(m_server->requestedPaths(), QStringList{c_latestPath});
  QCOMPARE(m_server->requestedMethods(), QStringList{QStringLiteral("HEAD")});
}

void TestUpdateService::testReleaseNotes_data() {
  QTest::addColumn<QString>("source");
  QTest::newRow("github") << QStringLiteral("github");
  QTest::newRow("gitee") << QStringLiteral("gitee");
}

void TestUpdateService::testReleaseNotes() {
  QFETCH(QString, source);
  publishRelease(QStringLiteral("4.4.3"));
  const bool github = source == QLatin1String("github");
  const QString path = github ? c_feedPath : c_notesPath;
  const QString markdown = QStringLiteral("## Release\n\n* **Fixed** `code`\n    * 中文 & notes\n\n"
                                          "Raw <b>literal</b> & &#10; stays encoded");
  QByteArray body =
      github ? githubFeed(QStringLiteral("4.4.3"),
                          QStringLiteral("<h2>Release</h2><ul><li><strong>Fixed</strong> "
                                         "<code>code</code><ul><li>中文 &amp; notes</li>"
                                         "</ul></li></ul>"))
             : giteePage(QStringLiteral("4.4.3"), markdown);
  // Textarea raw-text semantics also preserve literal angle brackets and ampersands.
  if (!github) {
    body.replace("&lt;b&gt;literal&lt;/b&gt;", "<b>literal</b>");
    body.replace("&amp; notes", "& notes");
  }
  m_server->serveBody(path, body, true);
  m_service.reset(makeService(QStringLiteral("4.4.2")));
  m_service->setSource(UpdateService::sourceFromString(source));
  const Outcome outcome = runCheck(m_service.data());
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY(outcome.info.updateAvailable);
  QCOMPARE(outcome.info.latestVersion, QStringLiteral("4.4.3"));
  if (github) {
    QVERIFY(outcome.info.releaseNotes.contains(QStringLiteral("## Release")));
    QVERIFY(outcome.info.releaseNotes.contains(QStringLiteral("**Fixed**")));
    QVERIFY(outcome.info.releaseNotes.contains(QStringLiteral("`code`")));
    QVERIFY(outcome.info.releaseNotes.contains(QStringLiteral("中文 & notes")));
    QVERIFY(!outcome.info.releaseNotes.contains(QStringLiteral("Wrong release")));
    QVERIFY(!outcome.info.releaseNotes.contains(QStringLiteral("<strong>")));
  } else {
    QCOMPARE(outcome.info.releaseNotes, markdown);
  }
  QCOMPARE(m_server->requestedPaths(), (QStringList{c_latestPath, path}));
  QCOMPARE(m_server->requestedMethods(),
           (QStringList{QStringLiteral("HEAD"), QStringLiteral("GET")}));
}

void TestUpdateService::testUnavailableNotesPreserveUpdate_data() {
  QTest::addColumn<QString>("source");
  QTest::addColumn<QByteArray>("body");
  QTest::addColumn<int>("status");
  QTest::newRow("github-missing-version")
      << QStringLiteral("github") << githubFeed(QStringLiteral("4.4.4"), QStringLiteral("Wrong"))
      << 200;
  QTest::newRow("github-truncated")
      << QStringLiteral("github")
      << githubFeed(QStringLiteral("4.4.3"), QStringLiteral("Notes")).chopped(7) << 200;
  QTest::newRow("github-empty") << QStringLiteral("github")
                                << githubFeed(QStringLiteral("4.4.3"), QString()) << 200;
  QTest::newRow("github-unexpected-origin")
      << QStringLiteral("github")
      << githubFeed(QStringLiteral("4.4.3"), QStringLiteral("Wrong"))
             .replace("https://github.com/", "https://untrusted.invalid/")
      << 200;
  QTest::newRow("gitee-missing-version")
      << QStringLiteral("gitee") << giteePage(QStringLiteral("4.4.4"), QStringLiteral("Wrong"))
      << 200;
  QTest::newRow("gitee-empty") << QStringLiteral("gitee")
                               << giteePage(QStringLiteral("4.4.3"), QString()) << 200;
  QTest::newRow("gitee-no-textarea-before-next-release")
      << QStringLiteral("gitee")
      << (QByteArray("<div class='release-tag-item' data-tag='v4.4.3'></div>") +
          giteePage(QStringLiteral("4.4.2"), QStringLiteral("Wrong")))
      << 200;
  QTest::newRow("gitee-truncated-textarea")
      << QStringLiteral("gitee")
      << QByteArray("<div class='release-tag-item' data-tag='v4.4.3'>"
                    "<textarea class='content'>Truncated")
      << 200;
  QTest::newRow("gitee-http-error") << QStringLiteral("gitee") << QByteArray() << 503;
  QTest::newRow("github-http-error") << QStringLiteral("github") << QByteArray() << 429;
}

void TestUpdateService::testUnavailableNotesPreserveUpdate() {
  QFETCH(QString, source);
  QFETCH(QByteArray, body);
  QFETCH(int, status);
  publishRelease(QStringLiteral("4.4.3"));
  const QString path = source == QLatin1String("github") ? c_feedPath : c_notesPath;
  if (status == 200) {
    m_server->serveBody(path, body);
  } else {
    m_server->serveStatus(path, status);
  }
  m_service.reset(makeService(QStringLiteral("4.4.2")));
  m_service->setSource(UpdateService::sourceFromString(source));
  const Outcome outcome = runCheck(m_service.data());
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY(outcome.info.updateAvailable);
  QCOMPARE(outcome.info.latestVersion, QStringLiteral("4.4.3"));
  QVERIFY(outcome.info.releaseNotes.isEmpty());
  QCOMPARE(m_server->requestedPaths(), (QStringList{c_latestPath, path}));
}

void TestUpdateService::testOversizedNotesAreAborted() {
  publishRelease(QStringLiteral("4.4.3"));
  // Valid notes at the beginning must not be used from an oversized response.
  // No Content-Length or terminal chunk/EOF: the streaming cap must abort promptly.
  m_server->serveBody(c_notesPath,
                      giteePage(QStringLiteral("4.4.3"), QStringLiteral("Discard these notes")) +
                          QByteArray(2 * 1024 * 1024, ' '),
                      true, true);
  m_service.reset(makeService(QStringLiteral("4.4.2")));
  QElapsedTimer timer;
  timer.start();
  const Outcome outcome = runCheck(m_service.data());
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY(outcome.info.updateAvailable);
  QVERIFY(outcome.info.releaseNotes.isEmpty());
  QVERIFY2(timer.elapsed() < 5000, "oversized notes waited for the notes timeout");
}

void TestUpdateService::testNotesTimeoutPreservesUpdate() {
  publishRelease(QStringLiteral("4.4.3"));
  m_server->serveBody(c_notesPath,
                      giteePage(QStringLiteral("4.4.3"), QStringLiteral("Late notes")));
  m_server->setDelay(c_notesPath, 120000);
  m_service.reset(makeService(QStringLiteral("4.4.2")));
  const Outcome outcome = runCheck(m_service.data(), 20000);
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY(outcome.info.updateAvailable);
  QCOMPARE(outcome.info.latestVersion, QStringLiteral("4.4.3"));
  QVERIFY(outcome.info.releaseNotes.isEmpty());
}

void TestUpdateService::testNotesRedirectLoopIsBounded() {
  publishRelease(QStringLiteral("4.4.3"));
  m_server->serveRedirect(c_notesPath, c_notesPath);
  m_service.reset(makeService(QStringLiteral("4.4.2")));
  const Outcome outcome = runCheck(m_service.data());
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY(outcome.info.updateAvailable);
  QVERIFY(outcome.info.releaseNotes.isEmpty());
  QCOMPARE(m_server->requestedPaths().count(c_latestPath), 1);
  QCOMPARE(m_server->requestedPaths().count(c_notesPath), UpdateService::c_maxRedirects + 1);
}

void TestUpdateService::testNotesRedirectPolicy_data() {
  QTest::addColumn<QString>("target");
  QTest::addColumn<bool>("allowed");
  QTest::newRow("same-notes-path") << c_notesPath + QLatin1Char('/') << true;
  QTest::newRow("archive") << QStringLiteral("/vnotex/vnote/repository/archive/v4.4.3") << false;
  QTest::newRow("tag") << QStringLiteral("/vnotex/vnote/releases/tag/v4.4.3") << false;
  QTest::newRow("asset") << QStringLiteral("/vnotex/vnote/releases/download/v4.4.3/VNote.zip")
                         << false;
  QTest::newRow("api") << QStringLiteral("/api/v5/repos/vnotex/vnote/releases/latest") << false;
  QTest::newRow("query") << c_notesPath + QStringLiteral("?download=1") << false;
  QTest::newRow("cross-source") << QStringLiteral("https://github.com/vnotex/vnote/releases.atom")
                                << false;
  QTest::newRow("outside-fixture")
      << QStringLiteral("https://gitee.com/vnotex/vnote/releases") << false;
}

void TestUpdateService::testNotesRedirectPolicy() {
  QFETCH(QString, target);
  QFETCH(bool, allowed);
  publishRelease(QStringLiteral("4.4.3"));
  m_server->serveRedirect(c_notesPath, target);
  if (allowed) {
    m_server->serveBody(target,
                        giteePage(QStringLiteral("4.4.3"), QStringLiteral("Release notes")));
  }
  m_service.reset(makeService(QStringLiteral("4.4.2")));
  const Outcome outcome = runCheck(m_service.data());
  QVERIFY2(outcome.ok, qPrintable(outcome.error));
  QVERIFY(outcome.info.updateAvailable);
  QCOMPARE(outcome.info.releaseNotes, allowed ? QStringLiteral("Release notes") : QString());
  QStringList expected{c_latestPath, c_notesPath};
  if (allowed) {
    expected.append(target);
  }
  QCOMPARE(m_server->requestedPaths(), expected);
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

void TestUpdateService::testDiscoveryDoesNotFollowTagOrReadBody_data() {
  QTest::addColumn<QString>("source");
  QTest::newRow("github") << QStringLiteral("github");
  QTest::newRow("gitee") << QStringLiteral("gitee");
}

void TestUpdateService::testDiscoveryDoesNotFollowTagOrReadBody() {
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
  QCOMPARE(
      m_server->requestedPaths(),
      (QStringList{c_latestPath, source == QLatin1String("github") ? c_feedPath : c_notesPath}));
  QCOMPARE(m_server->requestedMethods(),
           (QStringList{QStringLiteral("HEAD"), QStringLiteral("GET")}));
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
           (QStringList{c_latestPath, c_latestPath + QLatin1Char('/'), c_notesPath}));
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
void TestUpdateService::testCancelDuringACheckReturnsPromptly_data() {
  QTest::addColumn<QString>("cancelPath");
  QTest::newRow("discovery") << c_latestPath;
  QTest::newRow("notes") << c_notesPath;
}

void TestUpdateService::testCancelDuringACheckReturnsPromptly() {
  QFETCH(QString, cancelPath);
  publishRelease(QStringLiteral("4.4.3"));
  m_server->serveBody(c_notesPath, giteePage(QStringLiteral("4.4.3"), QStringLiteral("Notes")));
  m_server->setDelay(cancelPath, 120000);

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
  const auto onRequest =
      connect(m_server, &TestHttpServer::requestReceived, &loop,
              [this, &sinceRequest, &requestSeen, &cancelPath](const QString &p_path) {
                if (requestSeen || p_path != cancelPath) {
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
  m_server->serveBody(c_notesPath,
                      giteePage(QStringLiteral("4.4.3"), QStringLiteral("Gitee notes")));
  m_server->setDelay(c_notesPath, 2000);

  m_service.reset(makeService(QStringLiteral("4.4.2")));
  QCOMPARE(m_service->source(), UpdateService::Source::Gitee);

  QSignalSpy finished(m_service.data(), &UpdateService::checkFinished);
  m_service->checkForUpdates();
  QTest::qWait(300);

  m_service->setSource(UpdateService::Source::GitHub);
  QCOMPARE(m_service->source(), UpdateService::Source::Gitee);

  QVERIFY(finished.wait(20000));

  QCOMPARE(finished.at(0).at(0).value<UpdateInfo>().releaseNotes, QStringLiteral("Gitee notes"));
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
  m_server->serveBody(c_notesPath,
                      giteePage(QStringLiteral("4.4.3"), QStringLiteral("Release notes")));

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

// Notes conversion uses QtGui text documents, but no widgets or VTextEdit.
QTEST_MAIN(tests::TestUpdateService)
#include "test_updateservice.moc"
