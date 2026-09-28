#include "updatecontroller.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QSslSocket>
#include <QUrl>

#ifdef Q_OS_WIN
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QVersionNumber>
#include <windows.h>
#endif

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/servicelocator.h>
#include <core/services/notificationservice.h>
#include <widgets/dialogs/updatedialog.h>
#include <widgets/messageboxhelper.h>

using namespace vnotex;

namespace {

const QString c_updateCategory = QStringLiteral("update");

// One incident per offer: a later check replaces the message rather than
// stacking a second one for the same subject.
const QString c_offerDedupKey = QStringLiteral("update.available");

#ifdef Q_OS_WIN
const QString c_installDedupKey = QStringLiteral("update.install");
constexpr int c_scriptFrameLimit = 256;
constexpr int c_scriptStartupTimeoutMs = 15000;
#endif

} // namespace

#ifdef Q_OS_WIN
struct UpdateController::ScriptUpdateSession : QObject {
  enum class Phase { AwaitHello, Preparing, ReadyQueued, ShutdownRequested, Accepted, Failed };

  ScriptUpdateSession(const UpdateInfo &p_info, QObject *p_parent)
      : QObject(p_parent), m_info(p_info), m_server(new QLocalServer(this)),
        m_startupTimer(new QTimer(this)),
        m_scratch(QDir(QDir::tempPath()).filePath(QStringLiteral("vnote-update-XXXXXX"))),
        m_token(QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1()),
        m_helloFrame("HELLO " + m_token + '\n'), m_readyFrame("READY " + m_token + '\n') {
    m_startupTimer->setSingleShot(true);
  }

  UpdateInfo m_info;
  QLocalServer *m_server;
  QLocalSocket *m_socket = nullptr;
  QTimer *m_startupTimer;
  QTemporaryDir m_scratch;
  QByteArray m_token;
  QByteArray m_helloFrame;
  QByteArray m_readyFrame;
  QByteArray m_input;
  qint64 m_helperProcessId = 0;
  Phase m_phase = Phase::AwaitHello;
  // A dirty-document prompt may spin a nested event loop. If its pipe fails,
  // hold this failed attempt until the synchronous close request returns.
  bool m_requestingShutdown = false;
};
#endif

UpdateController::UpdateController(ServiceLocator &p_services, QWidget *p_parentWidget,
                                   QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_parentWidget(p_parentWidget) {
  auto *service = m_services.get<UpdateService>();
  if (!service) {
    return;
  }

  applyConfiguredSource();

  connect(service, &UpdateService::checkFinished, this, &UpdateController::onCheckFinished);
  connect(service, &UpdateService::failed, this, &UpdateController::onFailed);

  // A tracked message can leave the store without being dismissed: the
  // retention cap can evict it. Without this, m_offerNotificationId would keep
  // naming a message that no longer exists.
  if (auto *notifications = m_services.get<NotificationService>()) {
    connect(notifications, &NotificationService::messageRemoved, this, [this](quint64 p_id) {
      if (m_offerNotificationId != 0 && p_id == m_offerNotificationId) {
        m_offerNotificationId = 0;
      }
    });
  }
}

UpdateController::~UpdateController() = default;

void UpdateController::applyConfiguredSource() {
  auto *service = m_services.get<UpdateService>();
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (!service || !configMgr) {
    return;
  }
  // The service deliberately never reads config itself (core_configs links
  // core_services, so the reverse dependency would be a CMake cycle).
  service->setSource(UpdateService::sourceFromString(configMgr->getCoreConfig().getUpdateSource()));
}

// ===========================================================================
// Startup
// ===========================================================================

void UpdateController::runStartupTasks() {
  auto *service = m_services.get<UpdateService>();
  if (!service) {
    return;
  }

  auto *configMgr = m_services.get<ConfigMgr2>();
  if (!configMgr) {
    return;
  }
  auto &coreConfig = configMgr->getCoreConfig();
  if (!coreConfig.isCheckForUpdatesOnStartEnabled()) {
    return;
  }

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (!coreConfig.isUpdateCheckDue(now, CoreConfig::c_updateCheckIntervalMs)) {
    return;
  }

  startCheck(false);
}

// ===========================================================================
// Checking
// ===========================================================================

void UpdateController::checkForUpdatesManually() {
  if (!m_services.get<UpdateService>()) {
    openReleasesPage();
    return;
  }

  if (!startCheck(true)) {
    // A check is already running. Do NOT fall through to a message box: the
    // running check will report itself in a moment, and its own surface is
    // whatever it was started as.
    qInfo() << "update: a check is already running; ignoring the manual request";
  }
}

bool UpdateController::startCheck(bool p_manual) {
  auto *service = m_services.get<UpdateService>();
  if (!service) {
    return false;
  }

  if (m_checkInFlight) {
    // Refuse BEFORE touching m_manualCheck. The service would drop this request
    // anyway, but setting the mode for a request that never ran is exactly how
    // a silent startup failure ends up in a modal warning box.
    return false;
  }

  // A new check supersedes whatever the previous one advertised, so its button
  // must not outlive it. Dropped here, in ONE place, rather than reconciled
  // afterwards.
  invalidateTrackedNotification();

  // Pick up a Settings change without a restart. Ignored by the service while
  // a check is in flight, which is the correct behavior.
  applyConfiguredSource();

  if (!service->checkForUpdates()) {
    // Defense in depth: the service refused (its own busy flag is still set
    // from a check whose terminal signal has not been delivered yet). Leave
    // every piece of controller state alone.
    qWarning() << "update: the service refused a check request";
    return false;
  }

  m_manualCheck = p_manual;
  m_checkInFlight = true;

  // Advance the throttle on check START, not on completion: a failing network
  // must not cause a check on every single launch. Manual checks bypass the
  // throttle entirely but still record the timestamp.
  if (auto *configMgr = m_services.get<ConfigMgr2>()) {
    configMgr->getCoreConfig().setLastUpdateCheckTime(QDateTime::currentMSecsSinceEpoch());
  }

  return true;
}

void UpdateController::onCheckFinished(const vnotex::UpdateInfo &p_info) {
  // Consume the in-flight claim FIRST: m_manualCheck describes this result and
  // nothing else from here on.
  const bool manual = m_manualCheck;
  m_checkInFlight = false;

  if (!p_info.updateAvailable) {
    if (manual) {
      showDialog(p_info);
    }
    return;
  }

  if (!manual && isVersionSkipped(p_info.latestVersion)) {
    return;
  }

  if (!manual) {
    // Silent startup check: surface it as a notification rather than stealing
    // focus with a modal dialog.
    auto *notifications = m_services.get<NotificationService>();
    if (!notifications) {
      return;
    }

    NotificationMessage message;
    message.m_title = tr("Update Available");
#ifdef Q_OS_WIN
    ++m_offerGeneration;
    message.m_text =
        tr("VNote %1 is available. Update Now downloads the update, then closes and reopens VNote.")
            .arg(p_info.latestVersion);
#else
    message.m_text = tr("VNote %1 is available. Open the release page to download it.")
                         .arg(p_info.latestVersion);
#endif
    message.m_severity = NotificationMessage::Severity::Info;
    // Persist: the user must be able to find this whenever they are ready.
    message.m_duration = NotificationMessage::Duration::Persist;
    message.m_actions.append(makeCheckReleaseAction(p_info));
#ifdef Q_OS_WIN
    message.m_actions.append(makeScriptUpdateAction(p_info));
#endif
    message.m_category = c_updateCategory;
    message.m_dedupKey = c_offerDedupKey;
    // A brand-new offer the user has not seen: this earns the interruption.
    // renotify() rather than notify(), because the toast is raised ONLY by
    // messageAdded(Interrupt) -- folding into a live message would emit
    // messageUpdated and never be seen.
    message.m_attention = NotificationMessage::Attention::Interrupt;
    m_offerNotificationId = notifications->renotify(message);
    return;
  }

  showDialog(p_info);
}

void UpdateController::invalidateTrackedNotification() {
  if (m_offerNotificationId == 0) {
    return;
  }
  if (auto *notifications = m_services.get<NotificationService>()) {
    notifications->dismiss(m_offerNotificationId);
  }
  m_offerNotificationId = 0;
}

NotificationAction UpdateController::makeCheckReleaseAction(const UpdateInfo &p_info) const {
  NotificationAction check;
  check.m_label = tr("Check Release");

  QString url = p_info.releaseUrl;
  if (url.isEmpty()) {
    if (auto *service = m_services.get<UpdateService>()) {
      url = service->releasesPageUrl().toString();
    }
  }
  check.m_callback = [url]() {
    if (!url.isEmpty()) {
      QDesktopServices::openUrl(QUrl(url));
    }
  };
  return check;
}

#ifdef Q_OS_WIN
void UpdateController::testSetScriptLauncher(
    const std::function<bool(const QString &, const QStringList &, const QString &, qint64 *)>
        &p_launcher) {
  m_scriptLauncher = p_launcher;
}

void UpdateController::testSetScriptInstallDir(const QString &p_dir) { m_scriptInstallDir = p_dir; }

NotificationAction UpdateController::makeScriptUpdateAction(const UpdateInfo &p_info) {
  NotificationAction action;
  action.m_label = tr("Update Now");
  action.m_dismissOnTrigger = false;
  const QPointer<UpdateController> controller(this);
  const quint64 generation = m_offerGeneration;
  action.m_callback = [controller, p_info, generation]() {
    if (!controller || controller->m_offerGeneration != generation ||
        controller->m_offerNotificationId == 0) {
      return;
    }
    auto *notifications = controller->m_services.get<NotificationService>();
    if (notifications && notifications->isActive(controller->m_offerNotificationId)) {
      controller->startScriptUpdate(p_info);
    }
  };
  return action;
}

void UpdateController::startScriptUpdate(const UpdateInfo &p_info) {
  if (m_scriptUpdateSession) {
    return;
  }
  if (auto *notifications = m_services.get<NotificationService>()) {
    notifications->dismissByDedupKey(c_installDedupKey);
  }

  // Snapshot settings at activation, not when this offer was discovered.
  auto *configMgr = m_services.get<ConfigMgr2>();
  const QString source = UpdateService::sourceToString(UpdateService::sourceFromString(
      configMgr ? configMgr->getCoreConfig().getUpdateSource() : QString()));
  const QRegularExpression stableVersion(QStringLiteral("\\A[0-9]+\\.[0-9]+\\.[0-9]+\\z"));
  const QVersionNumber latest = QVersionNumber::fromString(p_info.latestVersion);
  const QVersionNumber current = QVersionNumber::fromString(p_info.currentVersion);
  if (!stableVersion.match(p_info.latestVersion).hasMatch() ||
      !stableVersion.match(p_info.currentVersion).hasMatch() || latest.segmentCount() != 3 ||
      current.segmentCount() != 3 || latest <= current) {
    reportScriptUpdateFailure(
        p_info,
        tr("Automatic update requires numeric three-component versions and a newer release."));
    return;
  }

#if !defined(Q_PROCESSOR_X86_64) || QT_POINTER_SIZE != 8
  reportScriptUpdateFailure(p_info, tr("Automatic update requires an x64 Windows build of VNote."));
  return;
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  const QString variant = QStringLiteral("win64");
#else
  const QString variant = QStringLiteral("win64-windows7");
#endif

  const bool injectedLauncher = static_cast<bool>(m_scriptLauncher);
  const QString requestedDir = injectedLauncher && !m_scriptInstallDir.isEmpty()
                                   ? m_scriptInstallDir
                                   : QCoreApplication::applicationDirPath();
  const QFileInfo installInfo(requestedDir);
  const QString installDir = installInfo.canonicalFilePath();
  const QFileInfo executable(QDir(installDir).filePath(QStringLiteral("vnote.exe")));
  const QFileInfo runningExecutable(QCoreApplication::applicationFilePath());
  if (!installInfo.isDir() || installDir.isEmpty() || !executable.isFile() ||
      executable.canonicalFilePath().compare(executable.absoluteFilePath(), Qt::CaseInsensitive) !=
          0 ||
      (!injectedLauncher && (runningExecutable.fileName().compare(QStringLiteral("vnote.exe"),
                                                                  Qt::CaseInsensitive) != 0 ||
                             runningExecutable.canonicalFilePath().compare(
                                 executable.canonicalFilePath(), Qt::CaseInsensitive) != 0))) {
    reportScriptUpdateFailure(
        p_info, tr("Automatic update must run from the installation's root vnote.exe."));
    return;
  }

  const QStringList helperNames{QStringLiteral("update-vnote.ps1"), QStringLiteral("minisign.exe"),
                                QStringLiteral("LICENSE.minisign")};
  const QDir helperDir(QDir(installDir).filePath(QStringLiteral("updater")));
  for (const auto &name : helperNames) {
    if (!QFileInfo(helperDir.filePath(name)).isFile()) {
      reportScriptUpdateFailure(p_info,
                                tr("The installed updater file is missing: %1")
                                    .arg(QDir::toNativeSeparators(helperDir.filePath(name))));
      return;
    }
  }

  wchar_t systemDirectory[MAX_PATH + 1];
  const UINT systemDirectorySize = GetSystemDirectoryW(systemDirectory, MAX_PATH + 1);
  if (systemDirectorySize == 0 || systemDirectorySize > MAX_PATH) {
    reportScriptUpdateFailure(p_info, tr("Could not locate the Windows system directory."));
    return;
  }
  const QString program =
      QDir(QString::fromWCharArray(systemDirectory, static_cast<int>(systemDirectorySize)))
          .filePath(QStringLiteral("WindowsPowerShell/v1.0/powershell.exe"));
  if (!QFileInfo(program).isFile()) {
    reportScriptUpdateFailure(
        p_info,
        tr("Windows PowerShell is unavailable at %1.").arg(QDir::toNativeSeparators(program)));
    return;
  }

  auto *session = new ScriptUpdateSession(p_info, this);
  m_scriptUpdateSession = session;
  if (!session->m_scratch.isValid()) {
    failScriptUpdate(session, tr("Could not create a private temporary updater directory: %1")
                                  .arg(session->m_scratch.errorString()));
    return;
  }
  const QDir scratch(session->m_scratch.path());
  for (const auto &name : helperNames) {
    QFile helper(helperDir.filePath(name));
    if (!helper.copy(scratch.filePath(name))) {
      failScriptUpdate(
          session,
          tr("Could not copy the installed updater file %1: %2").arg(name, helper.errorString()));
      return;
    }
  }

  const QString pipeName =
      QStringLiteral("vnote-update-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
  session->m_server->setSocketOptions(QLocalServer::UserAccessOption);
  session->m_server->setMaxPendingConnections(1);
  if (!session->m_server->listen(pipeName)) {
    failScriptUpdate(session, tr("Could not open the updater's private connection: %1")
                                  .arg(session->m_server->errorString()));
    return;
  }
  connect(session->m_server, &QLocalServer::newConnection, session,
          [this, session]() { acceptScriptUpdateConnections(session); });
  connect(session->m_startupTimer, &QTimer::timeout, session, [this, session]() {
    failScriptUpdate(
        session, tr("The updater did not connect within 15 seconds. Windows PowerShell may be "
                    "missing, blocked by Group Policy, or unable to start. Check its console."));
  });

  const QStringList arguments{
      QStringLiteral("-NoLogo"),
      QStringLiteral("-NoProfile"),
      QStringLiteral("-ExecutionPolicy"),
      QStringLiteral("Bypass"),
      QStringLiteral("-File"),
      QDir::toNativeSeparators(scratch.filePath(QStringLiteral("update-vnote.ps1"))),
      QStringLiteral("-Source"),
      source,
      QStringLiteral("-Version"),
      p_info.latestVersion,
      QStringLiteral("-CurrentVersion"),
      p_info.currentVersion,
      QStringLiteral("-Variant"),
      variant,
      QStringLiteral("-InstallDir"),
      QDir::toNativeSeparators(installDir),
      QStringLiteral("-ParentProcessId"),
      QString::number(QCoreApplication::applicationPid()),
      QStringLiteral("-PipeName"),
      pipeName,
      QStringLiteral("-Token"),
      QString::fromLatin1(session->m_token)};

  qint64 helperProcessId = 0;
  bool launched = false;
  QString launchError;
  if (injectedLauncher) {
    launched = m_scriptLauncher(program, arguments, scratch.path(), &helperProcessId);
    if (launched) {
      session->m_scratch.setAutoRemove(false);
    }
  } else {
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setWorkingDirectory(scratch.path());
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PATH"), QDir::toNativeSeparators(installDir) +
                                                   QLatin1Char(';') +
                                                   environment.value(QStringLiteral("PATH")));
    process.setProcessEnvironment(environment);
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *p_arguments) {
      p_arguments->flags &= ~DWORD(DETACHED_PROCESS | CREATE_NO_WINDOW);
      p_arguments->flags |= CREATE_NEW_CONSOLE;
      p_arguments->startupInfo->dwFlags &= ~DWORD(STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW);
    });
    launched = process.startDetached(&helperProcessId);
    if (launched) {
      session->m_scratch.setAutoRemove(false);
    } else {
      launchError = process.errorString();
    }
  }
  if (!launched) {
    failScriptUpdate(session, launchError.isEmpty()
                                  ? tr("Windows PowerShell could not be started.")
                                  : tr("Could not start Windows PowerShell: %1").arg(launchError));
    return;
  }

  // Cleanup ownership has transferred before processing HELLO: the helper may
  // create staging files only after OK. A timeout must leave its scratch intact.
  if (helperProcessId <= 0 || static_cast<quint64>(helperProcessId) > MAXDWORD) {
    failScriptUpdate(session, tr("The updater did not return a valid process identity."));
    return;
  }
  session->m_helperProcessId = helperProcessId;
  session->m_startupTimer->start(c_scriptStartupTimeoutMs);
  // A test launcher may have delivered newConnection while still starting.
  acceptScriptUpdateConnections(session);
}

void UpdateController::acceptScriptUpdateConnections(ScriptUpdateSession *p_session) {
  if (m_scriptUpdateSession != p_session || p_session->m_helperProcessId == 0 ||
      p_session->m_phase == ScriptUpdateSession::Phase::Failed) {
    return;
  }
  while (auto *socket = p_session->m_server->nextPendingConnection()) {
    ULONG processId = 0;
    if (p_session->m_socket ||
        !GetNamedPipeClientProcessId(reinterpret_cast<HANDLE>(socket->socketDescriptor()),
                                     &processId) ||
        static_cast<qint64>(processId) != p_session->m_helperProcessId) {
      socket->abort();
      socket->deleteLater();
      continue;
    }
    socket->setParent(p_session);
    socket->setReadBufferSize(c_scriptFrameLimit);
    p_session->m_socket = socket;
    connect(socket, &QLocalSocket::readyRead, p_session,
            [this, p_session]() { readScriptUpdateSocket(p_session); });
    connect(socket, &QLocalSocket::disconnected, p_session, [this, p_session]() {
      failScriptUpdate(p_session, tr("The updater disconnected before shutdown was accepted. "
                                     "Check the updater console for details."));
    });
    readScriptUpdateSocket(p_session);
    if (m_scriptUpdateSession != p_session ||
        p_session->m_phase == ScriptUpdateSession::Phase::Failed) {
      return;
    }
  }
}

void UpdateController::readScriptUpdateSocket(ScriptUpdateSession *p_session) {
  if (m_scriptUpdateSession != p_session || !p_session->m_socket ||
      p_session->m_phase == ScriptUpdateSession::Phase::Accepted ||
      p_session->m_phase == ScriptUpdateSession::Phase::Failed) {
    return;
  }
  auto *socket = p_session->m_socket;
  char bytes[c_scriptFrameLimit];
  while (socket->bytesAvailable() > 0) {
    const qint64 count = socket->read(bytes, sizeof(bytes));
    if (count <= 0) {
      failScriptUpdate(p_session, tr("Could not read the updater's private connection."));
      return;
    }
    for (qint64 i = 0; i < count; ++i) {
      const auto byte = static_cast<unsigned char>(bytes[i]);
      if (byte != '\n' && (byte < 0x20 || byte > 0x7e)) {
        failScriptUpdate(p_session, tr("The updater sent an invalid connection message."));
        return;
      }
      p_session->m_input.append(bytes[i]);
      if (p_session->m_input.size() >= c_scriptFrameLimit && byte != '\n') {
        failScriptUpdate(p_session,
                         tr("The updater's connection message exceeded its size limit."));
        return;
      }
      if (byte != '\n') {
        continue;
      }
      const bool hello = p_session->m_input == p_session->m_helloFrame;
      const bool ready = p_session->m_input == p_session->m_readyFrame;
      p_session->m_input.clear();
      using Phase = ScriptUpdateSession::Phase;
      if (hello && p_session->m_phase == Phase::AwaitHello) {
        p_session->m_phase = Phase::Preparing;
        p_session->m_startupTimer->stop();
        p_session->m_server->close();
        if (socket->write("OK\n") != 3) {
          failScriptUpdate(p_session,
                           tr("Could not acknowledge the updater's private connection."));
          return;
        }
        socket->flush();
      } else if (ready && p_session->m_phase == Phase::Preparing) {
        p_session->m_phase = Phase::ReadyQueued;
        // Leave the socket callback before entering a potentially nested modal
        // save prompt. Replays cannot enqueue a second request.
        QTimer::singleShot(0, p_session, [this, p_session]() {
          if (m_scriptUpdateSession != p_session || p_session->m_phase != Phase::ReadyQueued) {
            return;
          }
          p_session->m_phase = Phase::ShutdownRequested;
          p_session->m_requestingShutdown = true;
          const QPointer<UpdateController> controller(this);
          emit scriptUpdateShutdownRequested();
          if (controller && m_scriptUpdateSession == p_session) {
            p_session->m_requestingShutdown = false;
            if (p_session->m_phase == Phase::Failed) {
              retireScriptUpdate(p_session);
            }
          }
        });
      } else if (ready && (p_session->m_phase == Phase::ReadyQueued ||
                           p_session->m_phase == Phase::ShutdownRequested)) {
        // An authenticated READY is idempotent, including during close prompts.
        continue;
      } else {
        failScriptUpdate(p_session,
                         tr("The updater sent an unexpected or unauthenticated message."));
        return;
      }
    }
  }
}

void UpdateController::completeScriptUpdateShutdown(bool p_accepted) {
  auto *session = m_scriptUpdateSession;
  if (!session || session->m_phase != ScriptUpdateSession::Phase::ShutdownRequested) {
    return;
  }
  if (p_accepted) {
    // Commit the phase before flush(), which can deliver socket signals. Keep
    // this connection alive through application teardown even if writing fails:
    // the helper requires a complete ACCEPTED frame and otherwise fails closed.
    session->m_phase = ScriptUpdateSession::Phase::Accepted;
    session->m_socket->write("ACCEPTED\n");
    session->m_socket->flush();
    return;
  }
  session->m_socket->write("CANCELLED\n");
  session->m_socket->flush();
  // If the response has not drained yet, closing the pipe is also a refusal.
  retireScriptUpdate(session);
  if (auto *notifications = m_services.get<NotificationService>()) {
    notifications->dismissByDedupKey(c_installDedupKey);
  }
}

void UpdateController::retireScriptUpdate(ScriptUpdateSession *p_session) {
  if (m_scriptUpdateSession != p_session ||
      p_session->m_phase == ScriptUpdateSession::Phase::Accepted) {
    return;
  }
  m_scriptUpdateSession = nullptr;
  p_session->m_phase = ScriptUpdateSession::Phase::Failed;
  p_session->m_startupTimer->stop();
  p_session->m_server->close();
  if (p_session->m_socket) {
    p_session->m_socket->abort();
  }
  // readyRead/disconnected may be on the stack; never destroy their sender here.
  p_session->deleteLater();
}

void UpdateController::failScriptUpdate(ScriptUpdateSession *p_session, const QString &p_message) {
  if (m_scriptUpdateSession != p_session ||
      p_session->m_phase == ScriptUpdateSession::Phase::Accepted ||
      p_session->m_phase == ScriptUpdateSession::Phase::Failed) {
    return;
  }
  const UpdateInfo info = p_session->m_info;
  QString message = p_message;
  if (!p_session->m_scratch.autoRemove()) {
    message += QLatin1Char('\n') + tr("Temporary updater files were retained at %1.")
                                       .arg(QDir::toNativeSeparators(p_session->m_scratch.path()));
  }
  if (p_session->m_requestingShutdown) {
    p_session->m_phase = ScriptUpdateSession::Phase::Failed;
    p_session->m_startupTimer->stop();
    p_session->m_server->close();
    p_session->m_socket->abort();
  } else {
    retireScriptUpdate(p_session);
  }
  reportScriptUpdateFailure(info, message);
}

void UpdateController::reportScriptUpdateFailure(const UpdateInfo &p_info,
                                                 const QString &p_message) {
  if (auto *notifications = m_services.get<NotificationService>()) {
    NotificationMessage message;
    message.m_title = tr("Automatic Update Failed");
    message.m_text = tr("Automatic update is unavailable. Use Check Release to update manually.");
    message.m_details = p_message;
    message.m_severity = NotificationMessage::Severity::Warning;
    message.m_attention = NotificationMessage::Attention::Interrupt;
    message.m_duration = NotificationMessage::Duration::Persist;
    message.m_category = c_updateCategory;
    message.m_dedupKey = c_installDedupKey;
    message.m_actions.append(makeCheckReleaseAction(p_info));
    notifications->renotify(message);
  }
}
#endif

void UpdateController::showDialog(const UpdateInfo &p_info) {
  // A dialog left open from an EARLIER manual check describes a stale result.
  // Replace it rather than raising it, so the window on screen always matches
  // the check that just completed.
  if (m_dialog) {
    m_dialog->close();
    delete m_dialog.data();
  }

  auto *dialog = new UpdateDialog(p_info, m_parentWidget);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  m_dialog = dialog;

  connect(dialog, &UpdateDialog::skipRequested, this,
          [this](const QString &p_version) { skipVersion(p_version); });

  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void UpdateController::onFailed(const QString &p_message) {
  const bool manual = m_manualCheck;
  m_checkInFlight = false;

  if (!manual) {
    // Silent startup check: it must never interrupt the user, and it must never
    // write into a dialog that belongs to some earlier MANUAL check.
    qWarning() << "update check failed:" << p_message;
    return;
  }

  QString message = p_message;
  if (!QSslSocket::supportsSsl()) {
    // Without this the user sees a raw Qt errorString() ("TLS initialization
    // failed") with no way to act on it. The DLL names are Qt 5 / Windows
    // specific; every other configuration gets the generic wording.
#if defined(Q_OS_WIN) && (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    message += QLatin1Char('\n');
    message += tr("TLS is unavailable: OpenSSL could not be loaded. "
                  "libssl-1_1-x64.dll and libcrypto-1_1-x64.dll are expected "
                  "next to vnote.exe.");
#else
    message += QLatin1Char('\n');
    message += tr("TLS is unavailable: no working secure-socket backend was found.");
#endif
  }

  if (m_dialog) {
    m_dialog->setFailed(message);
    return;
  }

  MessageBoxHelper::notify(MessageBoxHelper::Warning, tr("Could not check for updates."), message,
                           QString(), m_parentWidget);
}

// ===========================================================================
// Policy helpers
// ===========================================================================

bool UpdateController::isVersionSkipped(const QString &p_version) const {
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (!configMgr) {
    return false;
  }
  const QString skipped = configMgr->getCoreConfig().getSkippedUpdateVersion();
  return !skipped.isEmpty() && skipped == p_version;
}

void UpdateController::skipVersion(const QString &p_version) {
  if (auto *configMgr = m_services.get<ConfigMgr2>()) {
    configMgr->getCoreConfig().setSkippedUpdateVersion(p_version);
  }
}

void UpdateController::openReleasesPage() const {
  if (auto *service = m_services.get<UpdateService>()) {
    QDesktopServices::openUrl(service->releasesPageUrl());
    return;
  }
  QDesktopServices::openUrl(QUrl(QStringLiteral("https://gitee.com/vnotex/vnote/releases")));
}
