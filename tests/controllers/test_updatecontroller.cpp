// UpdateController: notification wiring for the update-available offer.
//
// SCOPE. The controller's outcomes are private slots driven by UpdateService
// signals. This suite injects those signals directly rather than standing up a
// network fixture (tests/core/test_updateservice.cpp owns the mechanism), and
// covers what the controller alone decides:
//
//   * an available update becomes ONE interrupting, persistent notification
//     with a release-page fallback and an opt-in Windows script action;
//   * an up-to-date result is silent on a startup check;
//   * a later check supersedes the previous offer instead of stacking a second
//     one;
//   * the tracked-id bookkeeping when the retention cap evicts the message;
//   * the Windows launch-scoped, PID/token-authenticated shutdown handshake.
//
// The "Check Release" action calls QDesktopServices::openUrl(), which would open
// a real browser, so an `https` scheme URL handler is installed for the whole
// suite. That also lets the URL itself be asserted.

#include <QtTest>

#include <QApplication>
#include <QDesktopServices>
#include <QUrl>

#ifdef Q_OS_WIN
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalSocket>
#include <QProcess>
#include <QScopedPointer>
#include <QTemporaryDir>

#include <cstdio>
#endif

#include <controllers/updatecontroller.h>
#include <core/servicelocator.h>
#include <core/services/notificationservice.h>
#include <core/services/updateservice.h>

using namespace vnotex;

namespace tests {

namespace {

const QString c_offerKey = QStringLiteral("update.available");

UpdateInfo makeInfo(bool p_updateAvailable, const QString &p_latest = QStringLiteral("4.4.3")) {
  UpdateInfo info;
  info.updateAvailable = p_updateAvailable;
  info.currentVersion = QStringLiteral("4.4.2");
  info.latestVersion = p_latest;
  info.releaseNotes = QStringLiteral("notes");
  info.releaseUrl = QStringLiteral("https://gitee.com/vnotex/vnote/releases/tag/v%1").arg(p_latest);
  return info;
}

#ifdef Q_OS_WIN
const QString c_installKey = QStringLiteral("update.install");
const char *const c_scriptChildMode = "--update-script-child";

QString namedArgument(const QStringList &p_arguments, const QString &p_name) {
  const int index = p_arguments.indexOf(p_name);
  return index >= 0 && index + 1 < p_arguments.size() ? p_arguments.at(index + 1) : QString();
}

QByteArray readPipeFrame(QLocalSocket &p_socket, int p_timeout) {
  QElapsedTimer timer;
  timer.start();
  while (!p_socket.canReadLine()) {
    if (p_socket.state() == QLocalSocket::UnconnectedState) {
      return QByteArrayLiteral("CLOSED");
    }
    const int remaining = p_timeout - int(timer.elapsed());
    if (remaining <= 0 || !p_socket.waitForReadyRead(remaining)) {
      if (p_socket.canReadLine()) {
        break;
      }
      return p_socket.state() == QLocalSocket::UnconnectedState ? QByteArrayLiteral("CLOSED")
                                                                : QByteArrayLiteral("QUIET");
    }
  }
  return QByteArrayLiteral("FRAME ") + p_socket.readLine(257).toBase64();
}

QByteArray writePipeBytes(QLocalSocket &p_socket, const QByteArray &p_bytes) {
  if (p_socket.write(p_bytes) == p_bytes.size() &&
      (p_socket.bytesToWrite() == 0 || p_socket.waitForBytesWritten(5000))) {
    return QByteArrayLiteral("WROTE");
  }
  return p_socket.state() == QLocalSocket::UnconnectedState ? QByteArrayLiteral("CLOSED")
                                                            : QByteArrayLiteral("WRITE_FAILED");
}
#endif

} // namespace

#ifdef Q_OS_WIN
// A real pipe peer, controlled over QProcess stdin. No controller authentication
// seam is used: this process's PID must match the launcher's returned identity.
int runScriptChild() {
  const auto arguments = QCoreApplication::arguments();
  const auto pipeName = namedArgument(arguments, QStringLiteral("-PipeName"));
  // This fixture intentionally has no ConfigMgr2. A child must receive the
  // normalized Gitee source, not an empty or unrecognized source.
  if (pipeName.isEmpty() || namedArgument(arguments, QStringLiteral("-Token")).isEmpty() ||
      namedArgument(arguments, QStringLiteral("-Source")) != QLatin1String("gitee")) {
    return 2;
  }

  QLocalSocket socket;
  char commandBuffer[4096];
  while (std::fgets(commandBuffer, sizeof(commandBuffer), stdin)) {
    const QByteArray command = QByteArray(commandBuffer).trimmed();
    QByteArray response;
    if (command == "CONNECT" || command.startsWith("CONNECT ")) {
      socket.connectToServer(pipeName);
      if (!socket.waitForConnected(5000)) {
        response = "CONNECT_FAILED";
      } else if (command.startsWith("CONNECT ")) {
        response = writePipeBytes(socket, QByteArray::fromBase64(command.mid(8)));
      } else {
        response = "CONNECTED";
      }
    } else if (command.startsWith("WRITE ")) {
      response = writePipeBytes(socket, QByteArray::fromBase64(command.mid(6)));
    } else if (command == "READ") {
      response = readPipeFrame(socket, 5000);
    } else if (command == "PROBE") {
      response = readPipeFrame(socket, 150);
    } else if (command == "DISCONNECT") {
      socket.abort();
      response = "DISCONNECTED";
    } else {
      return 3;
    }
    std::fprintf(stdout, "%s\n", response.constData());
    std::fflush(stdout);
  }
  return 0;
}
#endif

// Intercepts QDesktopServices::openUrl() so the suite never opens a browser.
class UrlSink : public QObject {
  Q_OBJECT

public:
  QList<QUrl> m_opened;

public slots:
  void onUrl(const QUrl &p_url) { m_opened.append(p_url); }
};

class TestUpdateController : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void test_availableUpdateBecomesAnInterruptingReleasePageOffer();
  void test_upToDateResultIsSilentOnAStartupCheck();
  void test_aLaterCheckSupersedesThePreviousOffer();
  void test_aDroppedManualRequestCannotRelabelARunningCheck();
  void test_evictingTheTrackedMessageClearsTheTrackedId();
#ifdef Q_OS_WIN
  void test_launchFailureKeepsReleaseFallbackAndAllowsRetry();
  void test_duplicateActivationStartsOnlyOneHelper();
  void test_dismissedOfferCallbackDoesNotLaunch();
  void test_fragmentedHandshakeRequiresExplicitAcceptance();
  void test_invalidFramesNeverRequestShutdown_data();
  void test_invalidFramesNeverRequestShutdown();
  void test_wrongPeerPidCannotRequestShutdown();
  void test_cancelledShutdownAllowsRetry();
  void test_resetLauncherRestoresProductionIdentityChecks();
#endif

private:
  // A COPY: the store is rebuilt on every notify(), so holding a pointer across
  // a call would dangle.
  bool activeWithKey(const QString &p_key, NotificationMessage *p_out) const;

  ServiceLocator *m_services = nullptr;
  NotificationService *m_notifications = nullptr;
  UpdateService *m_updateService = nullptr;
  UpdateController *m_controller = nullptr;
  UrlSink *m_urlSink = nullptr;
#ifdef Q_OS_WIN
  void installScriptLauncher();
  QProcess *startScriptChild(const QStringList &p_arguments, const QString &p_workingDirectory);
  QByteArray childCommand(QProcess *p_child, const QByteArray &p_command);
  bool writeToScript(QProcess *p_child, const QByteArray &p_bytes);
  QByteArray readFromScript(QProcess *p_child);
  bool authenticateScript(QProcess *p_child);
  QByteArray requestFrame(const char *p_verb) const;
  void triggerScriptAction(const NotificationMessage &p_offer);

  QScopedPointer<QTemporaryDir> m_installDir;
  QVector<QProcess *> m_scriptChildren;
  QStringList m_launchArguments;
  QStringList m_scratchDirectories;
  int m_launchCount = 0;
  bool m_failLaunch = false;
#endif
};

bool TestUpdateController::activeWithKey(const QString &p_key, NotificationMessage *p_out) const {
  for (const auto &msg : m_notifications->messages()) {
    if (!msg.m_dismissed && msg.m_dedupKey == p_key) {
      *p_out = msg;
      return true;
    }
  }
  return false;
}

void TestUpdateController::init() {
  m_urlSink = new UrlSink();
  QDesktopServices::setUrlHandler(QStringLiteral("https"), m_urlSink, "onUrl");

  m_services = new ServiceLocator();
  m_notifications = new NotificationService();
  m_services->registerService<NotificationService>(m_notifications);

  m_updateService = new UpdateService(QStringLiteral("4.4.2"));
  m_services->registerService<UpdateService>(m_updateService);

  // No ConfigMgr2 and a null parent widget: runStartupTasks() then stops before
  // the throttle, which keeps the network check (and its modal failure box) out
  // of the way. The check-result path is driven directly through the service's
  // signals instead.
  m_controller = new UpdateController(*m_services, nullptr);
#ifdef Q_OS_WIN
  m_launchCount = 0;
  m_failLaunch = false;
  m_installDir.reset(new QTemporaryDir(QDir::tempPath() + QStringLiteral("/vnote script-XXXXXX")));
  QVERIFY(m_installDir->isValid());
  QVERIFY(QDir(m_installDir->path()).mkdir(QStringLiteral("updater")));
  const QStringList files{QStringLiteral("vnote.exe"), QStringLiteral("updater/update-vnote.ps1"),
                          QStringLiteral("updater/minisign.exe"),
                          QStringLiteral("updater/LICENSE.minisign")};
  for (const auto &name : files) {
    QFile file(m_installDir->filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("private controller fixture"), qint64(26));
  }
  m_controller->testSetScriptInstallDir(m_installDir->path());
  installScriptLauncher();
#endif
}

void TestUpdateController::cleanup() {
  delete m_controller;
  m_controller = nullptr;
#ifdef Q_OS_WIN
  for (auto *child : m_scriptChildren) {
    if (child->state() != QProcess::NotRunning) {
      child->kill();
      child->waitForFinished(5000);
    }
    delete child;
  }
  m_scriptChildren.clear();
  // Production transfers scratch ownership to the detached script. These
  // substitute children never install anything; remove their scratch only
  // after all owned child processes have stopped.
  for (const auto &path : m_scratchDirectories) {
    QDir(path).removeRecursively();
  }
  m_scratchDirectories.clear();
  m_launchArguments.clear();
  m_installDir.reset();
#endif
  delete m_updateService;
  m_updateService = nullptr;
  delete m_notifications;
  m_notifications = nullptr;
  delete m_services;
  m_services = nullptr;

  QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
  delete m_urlSink;
  m_urlSink = nullptr;
}

// The offer must be Interrupt (a toast is raised ONLY by messageAdded carrying
// Interrupt) and Persist (the user must be able to find it whenever they are
// ready). The first action must remain the release-page fallback on every platform.
void TestUpdateController::test_availableUpdateBecomesAnInterruptingReleasePageOffer() {
  emit m_updateService->checkFinished(makeInfo(true));

  NotificationMessage offer;
  QVERIFY2(activeWithKey(c_offerKey, &offer), "no offer notification was posted");
  QCOMPARE(offer.m_category, QStringLiteral("update"));
  QCOMPARE(offer.m_attention, NotificationMessage::Attention::Interrupt);
  QCOMPARE(offer.m_duration, NotificationMessage::Duration::Persist);
#ifdef Q_OS_WIN
  QCOMPARE(offer.m_actions.size(), 2);
#else
  QCOMPARE(offer.m_actions.size(), 1);
#endif
  QVERIFY2(offer.m_text.contains(QStringLiteral("4.4.3")), qPrintable(offer.m_text));

  offer.m_actions.at(0).m_callback();
  QCOMPARE(m_urlSink->m_opened.size(), 1);
  QCOMPARE(m_urlSink->m_opened.at(0).toString(),
           QStringLiteral("https://gitee.com/vnotex/vnote/releases/tag/v4.4.3"));
}

void TestUpdateController::test_upToDateResultIsSilentOnAStartupCheck() {
  emit m_updateService->checkFinished(makeInfo(false));

  NotificationMessage ignored;
  QVERIFY2(!activeWithKey(c_offerKey, &ignored), "an up-to-date check posted a notification");
  QCOMPARE(m_notifications->activeCount(), 0);
  QCOMPARE(m_urlSink->m_opened.size(), 0);
}

// A new check supersedes whatever the previous one advertised: its button would
// otherwise point at a release that is no longer the latest. The offer is one
// incident, not a growing pile.
void TestUpdateController::test_aLaterCheckSupersedesThePreviousOffer() {
  emit m_updateService->checkFinished(makeInfo(true, QStringLiteral("4.4.3")));

  NotificationMessage first;
  QVERIFY(activeWithKey(c_offerKey, &first));
  const quint64 firstId = first.m_id;

  emit m_updateService->checkFinished(makeInfo(true, QStringLiteral("4.4.4")));

  NotificationMessage second;
  QVERIFY(activeWithKey(c_offerKey, &second));
  QVERIFY2(second.m_id != firstId, "the second offer reused the first message");
  QVERIFY2(second.m_text.contains(QStringLiteral("4.4.4")), qPrintable(second.m_text));
  // renotify() retires the previous generation rather than leaving two rows.
  QVERIFY2(!m_notifications->isActive(firstId), "the superseded offer is still active");
  QCOMPARE(m_notifications->activeCount(), 1);
#ifdef Q_OS_WIN
  triggerScriptAction(first);
  QCOMPARE(m_launchCount, 0);
  triggerScriptAction(second);
  QCOMPARE(m_launchCount, 1);
  QVERIFY(authenticateScript(m_scriptChildren.last()));
#endif
}

// The regression this guards: `startCheck()` used to set the manual/startup
// mode BEFORE knowing whether the service accepted the request. A user clicking
// "Check for Updates" while the silent startup check was still running would
// therefore have that startup check's result rendered on the MANUAL surface --
// a modal dialog for a check they never saw start, and, worse, a modal warning
// box for a background failure that is supposed to be silent.
//
// The service is pointed at an unroutable endpoint so its check stays in flight
// (and eventually fails) without any fixture server.
void TestUpdateController::test_aDroppedManualRequestCannotRelabelARunningCheck() {
  m_updateService->testSetEndpointOverride(
      QUrl(QStringLiteral("https://api.github.com/repos/vnotex/vnote/releases/latest")));

  // Start a silent (startup-style) check by driving the service directly, then
  // tell the controller a manual request arrived. The service must refuse it.
  QVERIFY2(m_updateService->checkForUpdates(), "the first check was not accepted");
  QVERIFY2(!m_updateService->checkForUpdates(),
           "the service accepted a second concurrent check, so this test proves nothing");

  m_controller->checkForUpdatesManually();

  // The controller must not have adopted the manual mode from a request that
  // never ran: a startup-style failure stays silent (no message box, nothing
  // posted) rather than being reported as a manual check's failure.
  emit m_updateService->failed(QStringLiteral("boom"));

  NotificationMessage ignored;
  QVERIFY2(!activeWithKey(c_offerKey, &ignored), "a failure posted an offer notification");
  QCOMPARE(m_notifications->activeCount(), 0);
  QCOMPARE(m_urlSink->m_opened.size(), 0);

  // And an offer arriving for that same startup check stays on the silent
  // surface too -- i.e. it becomes a notification, not a modal dialog.
  emit m_updateService->checkFinished(makeInfo(true));
  NotificationMessage offer;
  QVERIFY2(activeWithKey(c_offerKey, &offer),
           "the startup result did not land on the notification surface");
}

// A tracked message can leave the store without being dismissed (the retention
// cap evicts it). The controller subscribes to messageRemoved so
// m_offerNotificationId cannot keep naming a message that no longer exists.
void TestUpdateController::test_evictingTheTrackedMessageClearsTheTrackedId() {
  emit m_updateService->checkFinished(makeInfo(true));

  NotificationMessage offer;
  QVERIFY(activeWithKey(c_offerKey, &offer));
  const quint64 offerId = offer.m_id;

  // Push the store past the cap. The offer carries an action AND is Persist, so
  // the "cheap to lose" eviction tier would always prefer a plain filler over
  // it; the fillers therefore have to be equally expensive, which leaves the
  // evictor with its last resort -- the OLDEST active message, i.e. the offer.
  auto expensiveFiller = [](const QString &p_text) {
    NotificationMessage filler;
    filler.m_text = p_text;
    filler.m_duration = NotificationMessage::Duration::Persist;
    NotificationAction action;
    action.m_label = QStringLiteral("noop");
    action.m_callback = []() {};
    filler.m_actions.append(action);
    return filler;
  };

  int i = 0;
  while (m_notifications->messages().size() < NotificationService::c_maxMessages) {
    m_notifications->notify(expensiveFiller(QStringLiteral("filler %1").arg(i++)));
  }
  m_notifications->notify(expensiveFiller(QStringLiteral("overflow")));

  QVERIFY2(!m_notifications->isActive(offerId), "precondition: the offer was not evicted");
  QVERIFY2(m_notifications->messages().size() <= NotificationService::c_maxMessages,
           "the store grew past the cap");

#ifdef Q_OS_WIN
  triggerScriptAction(offer);
  QCOMPARE(m_launchCount, 0);
#endif

  // With the tracked id cleared, a fresh offer lands as a NEW active message
  // instead of trying to dismiss a dead one.
  emit m_updateService->checkFinished(makeInfo(true));
  NotificationMessage second;
  QVERIFY(activeWithKey(c_offerKey, &second));
  QVERIFY(second.m_id != offerId);
#ifdef Q_OS_WIN
  triggerScriptAction(second);
  QCOMPARE(m_launchCount, 1);
  QVERIFY(authenticateScript(m_scriptChildren.last()));
#endif
}

#ifdef Q_OS_WIN
void TestUpdateController::installScriptLauncher() {
  m_controller->testSetScriptLauncher([this](const QString &, const QStringList &p_arguments,
                                             const QString &p_workingDirectory, qint64 *p_pid) {
    ++m_launchCount;
    m_launchArguments = p_arguments;
    m_scratchDirectories.append(p_workingDirectory);
    if (m_failLaunch) {
      return false;
    }
    auto *child = startScriptChild(p_arguments, p_workingDirectory);
    if (child->state() == QProcess::NotRunning) {
      return false;
    }
    *p_pid = child->processId();
    return true;
  });
}

QProcess *TestUpdateController::startScriptChild(const QStringList &p_arguments,
                                                 const QString &p_workingDirectory) {
  auto *child = new QProcess();
  m_scriptChildren.append(child);
  child->setProgram(QCoreApplication::applicationFilePath());
  child->setArguments(QStringList{QString::fromLatin1(c_scriptChildMode)} + p_arguments);
  child->setWorkingDirectory(p_workingDirectory);
  child->start();
  child->waitForStarted(5000);
  return child;
}

QByteArray TestUpdateController::childCommand(QProcess *p_child, const QByteArray &p_command) {
  if (p_child->write(p_command + '\n') < 0 ||
      !QTest::qWaitFor(
          [p_child]() {
            return p_child->canReadLine() || p_child->state() == QProcess::NotRunning;
          },
          7000) ||
      !p_child->canReadLine()) {
    qWarning() << "Script child command failed:" << p_command << p_child->errorString()
               << p_child->readAllStandardError();
    return {};
  }
  return p_child->readLine().trimmed();
}

bool TestUpdateController::writeToScript(QProcess *p_child, const QByteArray &p_bytes) {
  return childCommand(p_child, QByteArrayLiteral("WRITE ") + p_bytes.toBase64()) == "WROTE";
}

QByteArray TestUpdateController::readFromScript(QProcess *p_child) {
  const auto response = childCommand(p_child, QByteArrayLiteral("READ"));
  return response.startsWith("FRAME ") ? QByteArray::fromBase64(response.mid(6)) : response;
}

bool TestUpdateController::authenticateScript(QProcess *p_child) {
  return childCommand(p_child, QByteArrayLiteral("CONNECT")) == "CONNECTED" &&
         writeToScript(p_child, requestFrame("HELLO")) && readFromScript(p_child) == "OK\n";
}

QByteArray TestUpdateController::requestFrame(const char *p_verb) const {
  return QByteArray(p_verb) + ' ' +
         namedArgument(m_launchArguments, QStringLiteral("-Token")).toLatin1() + '\n';
}

void TestUpdateController::triggerScriptAction(const NotificationMessage &p_offer) {
  QVERIFY(p_offer.m_actions.size() > 1);
  const auto action = p_offer.m_actions.at(1);
  QVERIFY(bool(action.m_callback));
  action.m_callback();
  // Honor the same dismissal contract as the notification surface, rather than
  // merely asserting a flag that a real activation might otherwise ignore.
  if (action.m_dismissOnTrigger) {
    m_notifications->dismiss(p_offer.m_id);
  }
}

void TestUpdateController::test_launchFailureKeepsReleaseFallbackAndAllowsRetry() {
  emit m_updateService->checkFinished(makeInfo(true));
  NotificationMessage offer;
  QVERIFY(activeWithKey(c_offerKey, &offer));
  QSignalSpy shutdown(m_controller, &UpdateController::scriptUpdateShutdownRequested);
  QSignalSpy added(m_notifications, &NotificationService::messageAdded);

  m_failLaunch = true;
  triggerScriptAction(offer);
  QCOMPARE(m_launchCount, 1);
  QCOMPARE(m_scriptChildren.size(), 0);
  QCOMPARE(shutdown.count(), 0);
  QCOMPARE(added.count(), 1);
  NotificationMessage failure;
  QVERIFY(activeWithKey(c_installKey, &failure));
  QCOMPARE(failure.m_category, QStringLiteral("update"));
  QCOMPARE(failure.m_attention, NotificationMessage::Attention::Interrupt);
  QCOMPARE(failure.m_severity, NotificationMessage::Severity::Warning);
  QVERIFY(m_notifications->isActive(offer.m_id));
  QVERIFY(!failure.m_actions.isEmpty());
  failure.m_actions.first().m_callback();
  offer.m_actions.first().m_callback();
  QCOMPARE(m_urlSink->m_opened,
           (QList<QUrl>{QUrl(makeInfo(true).releaseUrl), QUrl(makeInfo(true).releaseUrl)}));

  m_failLaunch = false;
  triggerScriptAction(offer);
  QCOMPARE(m_launchCount, 2);
  QCOMPARE(m_scriptChildren.size(), 1);
  QVERIFY(authenticateScript(m_scriptChildren.last()));
  QVERIFY(!activeWithKey(c_installKey, &failure));
  QVERIFY(m_notifications->isActive(offer.m_id));
  QCOMPARE(shutdown.count(), 0);
}

void TestUpdateController::test_duplicateActivationStartsOnlyOneHelper() {
  emit m_updateService->checkFinished(makeInfo(true));
  NotificationMessage offer;
  QVERIFY(activeWithKey(c_offerKey, &offer));
  QSignalSpy shutdown(m_controller, &UpdateController::scriptUpdateShutdownRequested);
  triggerScriptAction(offer);
  triggerScriptAction(offer);
  QCOMPARE(m_launchCount, 1);
  QCOMPARE(m_scriptChildren.size(), 1);
  QVERIFY(authenticateScript(m_scriptChildren.last()));
  triggerScriptAction(offer);
  QCOMPARE(m_launchCount, 1);
  QCOMPARE(shutdown.count(), 0);
  QVERIFY(m_notifications->isActive(offer.m_id));
}

void TestUpdateController::test_dismissedOfferCallbackDoesNotLaunch() {
  emit m_updateService->checkFinished(makeInfo(true));
  NotificationMessage offer;
  QVERIFY(activeWithKey(c_offerKey, &offer));
  m_notifications->dismiss(offer.m_id);
  triggerScriptAction(offer);
  QCOMPARE(m_launchCount, 0);

  emit m_updateService->checkFinished(makeInfo(true));
  NotificationMessage next;
  QVERIFY(activeWithKey(c_offerKey, &next));
  triggerScriptAction(offer);
  QCOMPARE(m_launchCount, 0);
  triggerScriptAction(next);
  QCOMPARE(m_launchCount, 1);
  QVERIFY(authenticateScript(m_scriptChildren.last()));
}

void TestUpdateController::test_fragmentedHandshakeRequiresExplicitAcceptance() {
  emit m_updateService->checkFinished(makeInfo(true));
  NotificationMessage offer;
  QVERIFY(activeWithKey(c_offerKey, &offer));
  QSignalSpy shutdown(m_controller, &UpdateController::scriptUpdateShutdownRequested);
  triggerScriptAction(offer);
  QCOMPARE(m_scriptChildren.size(), 1);
  auto *child = m_scriptChildren.last();
  QCOMPARE(childCommand(child, "CONNECT"), QByteArray("CONNECTED"));

  const auto hello = requestFrame("HELLO");
  QVERIFY(writeToScript(child, hello.left(3)));
  QCOMPARE(childCommand(child, "PROBE"), QByteArray("QUIET"));
  QCOMPARE(shutdown.count(), 0);
  QVERIFY(writeToScript(child, hello.mid(3)));
  QCOMPARE(readFromScript(child), QByteArray("OK\n"));
  QCOMPARE(shutdown.count(), 0);

  // Neither HELLO nor an unsolicited completion authorizes installation.
  m_controller->completeScriptUpdateShutdown(true);
  QCOMPARE(childCommand(child, "PROBE"), QByteArray("QUIET"));
  const auto ready = requestFrame("READY");
  QVERIFY(writeToScript(child, ready.left(ready.size() - 1)));
  QCOMPARE(childCommand(child, "PROBE"), QByteArray("QUIET"));
  QCOMPARE(shutdown.count(), 0);
  QVERIFY(writeToScript(child, ready.right(1)));
  QTRY_COMPARE(shutdown.count(), 1);
  QCOMPARE(childCommand(child, "PROBE"), QByteArray("QUIET"));

  QVERIFY(writeToScript(child, ready));
  QCOMPARE(childCommand(child, "PROBE"), QByteArray("QUIET"));
  QCOMPARE(shutdown.count(), 1);
  m_controller->completeScriptUpdateShutdown(true);
  QCOMPARE(readFromScript(child), QByteArray("ACCEPTED\n"));
  QVERIFY(writeToScript(child, ready));
  m_controller->completeScriptUpdateShutdown(false);
  QCOMPARE(childCommand(child, "PROBE"), QByteArray("QUIET"));
  QCOMPARE(shutdown.count(), 1);
  triggerScriptAction(offer);
  QCOMPARE(m_launchCount, 1);

  // An accepted attempt remains alive until application/controller teardown.
  delete m_controller;
  m_controller = nullptr;
  QCOMPARE(readFromScript(child), QByteArray("CLOSED"));
}

void TestUpdateController::test_invalidFramesNeverRequestShutdown_data() {
  QTest::addColumn<bool>("authenticateFirst");
  QTest::addColumn<QByteArray>("payload");
  QTest::newRow("wrong-hello-token") << false << QByteArray("HELLO wrong-token\n");
  QTest::newRow("ready-before-hello") << false << QByteArray("READY {token}\n");
  QTest::newRow("wrong-ready-token") << true << QByteArray("READY wrong-token\n");
  QTest::newRow("repeated-hello") << true << QByteArray("HELLO {token}\n");
  QTest::newRow("malformed-ready") << true << QByteArray("READY  {token}\n");
  QTest::newRow("non-ascii-ready") << true << (QByteArray("READY {token}") + char(0x80) + '\n');
  QTest::newRow("overlong-unterminated-frame") << true << QByteArray(257, 'X');
}

void TestUpdateController::test_invalidFramesNeverRequestShutdown() {
  QFETCH(bool, authenticateFirst);
  QFETCH(QByteArray, payload);
  emit m_updateService->checkFinished(makeInfo(true));
  NotificationMessage offer;
  QVERIFY(activeWithKey(c_offerKey, &offer));
  QSignalSpy shutdown(m_controller, &UpdateController::scriptUpdateShutdownRequested);
  triggerScriptAction(offer);
  QCOMPARE(m_scriptChildren.size(), 1);
  auto *child = m_scriptChildren.last();
  if (authenticateFirst) {
    QVERIFY(authenticateScript(child));
  } else {
    QCOMPARE(childCommand(child, "CONNECT"), QByteArray("CONNECTED"));
  }
  payload.replace("{token}", namedArgument(m_launchArguments, QStringLiteral("-Token")).toLatin1());
  QVERIFY(writeToScript(child, payload));
  QCOMPARE(readFromScript(child), QByteArray("CLOSED"));
  QCOMPARE(shutdown.count(), 0);
  NotificationMessage failure;
  QVERIFY(activeWithKey(c_installKey, &failure));
  QVERIFY(m_notifications->isActive(offer.m_id));

  triggerScriptAction(offer);
  QCOMPARE(m_launchCount, 2);
  QVERIFY(authenticateScript(m_scriptChildren.last()));
  QVERIFY(!activeWithKey(c_installKey, &failure));
  QCOMPARE(shutdown.count(), 0);
}

void TestUpdateController::test_wrongPeerPidCannotRequestShutdown() {
  emit m_updateService->checkFinished(makeInfo(true));
  NotificationMessage offer;
  QVERIFY(activeWithKey(c_offerKey, &offer));
  QSignalSpy shutdown(m_controller, &UpdateController::scriptUpdateShutdownRequested);
  triggerScriptAction(offer);
  QCOMPARE(m_scriptChildren.size(), 1);
  auto *helper = m_scriptChildren.last();
  auto *intruder = startScriptChild(m_launchArguments, helper->workingDirectory());
  QVERIFY(intruder->state() != QProcess::NotRunning);
  QVERIFY(intruder->processId() != helper->processId());
  const auto unauthorized = requestFrame("HELLO") + requestFrame("READY");
  const auto result = childCommand(intruder, "CONNECT " + unauthorized.toBase64());
  QVERIFY(result == "WROTE" || result == "CLOSED");
  QCOMPARE(readFromScript(intruder), QByteArray("CLOSED"));
  QCOMPARE(shutdown.count(), 0);

  // Knowing the token is insufficient, but rejecting the unrelated process
  // must not prevent the real launched helper from completing its handshake.
  QVERIFY(authenticateScript(helper));
  QVERIFY(writeToScript(helper, requestFrame("READY")));
  QTRY_COMPARE(shutdown.count(), 1);
  m_controller->completeScriptUpdateShutdown(false);
  QCOMPARE(readFromScript(helper), QByteArray("CANCELLED\n"));
  QCOMPARE(m_launchCount, 1);
}

void TestUpdateController::test_cancelledShutdownAllowsRetry() {
  emit m_updateService->checkFinished(makeInfo(true));
  NotificationMessage offer;
  QVERIFY(activeWithKey(c_offerKey, &offer));
  QSignalSpy shutdown(m_controller, &UpdateController::scriptUpdateShutdownRequested);
  triggerScriptAction(offer);
  QCOMPARE(m_scriptChildren.size(), 1);
  auto *first = m_scriptChildren.last();
  QVERIFY(authenticateScript(first));
  QVERIFY(writeToScript(first, requestFrame("READY")));
  QTRY_COMPARE(shutdown.count(), 1);
  m_controller->completeScriptUpdateShutdown(false);
  QCOMPARE(readFromScript(first), QByteArray("CANCELLED\n"));
  QCOMPARE(readFromScript(first), QByteArray("CLOSED"));
  QVERIFY(m_notifications->isActive(offer.m_id));

  triggerScriptAction(offer);
  QCOMPARE(m_launchCount, 2);
  QCOMPARE(m_scriptChildren.size(), 2);
  auto *second = m_scriptChildren.last();
  QVERIFY(authenticateScript(second));
  QVERIFY(writeToScript(second, requestFrame("READY")));
  QTRY_COMPARE(shutdown.count(), 2);
  m_controller->completeScriptUpdateShutdown(false);
  QCOMPARE(readFromScript(second), QByteArray("CANCELLED\n"));
}

void TestUpdateController::test_resetLauncherRestoresProductionIdentityChecks() {
  emit m_updateService->checkFinished(makeInfo(true));
  NotificationMessage offer;
  QVERIFY(activeWithKey(c_offerKey, &offer));
  triggerScriptAction(offer);
  QCOMPARE(m_scriptChildren.size(), 1);
  auto *child = m_scriptChildren.last();
  QVERIFY(authenticateScript(child));
  QCOMPARE(childCommand(child, "DISCONNECT"), QByteArray("DISCONNECTED"));
  NotificationMessage failure;
  QTRY_VERIFY(activeWithKey(c_installKey, &failure));

  // The private installed-root override must cease granting an exception for
  // this test executable once the custom launcher has been removed.
  m_controller->testSetScriptLauncher({});
  QSignalSpy added(m_notifications, &NotificationService::messageAdded);
  triggerScriptAction(offer);
  QCOMPARE(added.count(), 1);
  QVERIFY(activeWithKey(c_installKey, &failure));
  QCOMPARE(failure.m_severity, NotificationMessage::Severity::Warning);
  QCOMPARE(failure.m_attention, NotificationMessage::Attention::Interrupt);
  QCOMPARE(m_launchCount, 1);
  QVERIFY(m_notifications->isActive(offer.m_id));

  installScriptLauncher();
  triggerScriptAction(offer);
  QCOMPARE(m_launchCount, 2);
  QVERIFY(authenticateScript(m_scriptChildren.last()));
}
#endif

} // namespace tests

int main(int argc, char *argv[]) {
#ifdef Q_OS_WIN
  if (argc > 1 && QByteArray(argv[1]) == tests::c_scriptChildMode) {
    QCoreApplication app(argc, argv);
    return tests::runScriptChild();
  }
#endif
  QApplication app(argc, argv);
  app.setAttribute(Qt::AA_Use96Dpi, true);
#ifdef QT_KEYPAD_NAVIGATION
  QApplication::setNavigationMode(Qt::NavigationModeNone);
#endif
  QTEST_SET_MAIN_SOURCE_PATH
  tests::TestUpdateController testObject;
  return QTest::qExec(&testObject, argc, argv);
}
#include "test_updatecontroller.moc"
