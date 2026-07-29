#include "updatecontroller.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/servicelocator.h>
#include <core/services/notificationservice.h>
#include <widgets/dialogs/updatedialog.h>
#include <widgets/mainwindow2.h>
#include <widgets/messageboxhelper.h>

using namespace vnotex;

namespace {

const QString c_releasesPageUrl = QStringLiteral("https://github.com/vnotex/vnote/releases");

} // namespace

UpdateController::UpdateController(ServiceLocator &p_services, MainWindow2 *p_mainWindow,
                                   QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_mainWindow(p_mainWindow) {
  auto *service = m_services.get<UpdateService>();
  if (!service) {
    return;
  }

  connect(service, &UpdateService::checkFinished, this, &UpdateController::onCheckFinished);
  connect(service, &UpdateService::progress, this, &UpdateController::onProgress);
  connect(service, &UpdateService::readyToApply, this, &UpdateController::onReadyToApply);
  connect(service, &UpdateService::failed, this, &UpdateController::onFailed);
}

UpdateController::~UpdateController() = default;

// ===========================================================================
// Startup
// ===========================================================================

void UpdateController::runStartupTasks() {
  auto *service = m_services.get<UpdateService>();
  if (!service) {
    return;
  }

  // 1. An apply outcome cannot cross the restart through NotificationService
  //    (it is in-memory only), so it is persisted as result.json and turned
  //    into a notification here.
  consumeStoredResult();

  // 2. A staged update may already be waiting. revalidatePending() silently
  //    discards a plan that is invalid, superseded, or no longer verifies.
  if (service->revalidatePending()) {
    notifyPendingUpdate(service->pendingVersion());
    // Do not start a new check on top of an update that is ready to install.
    return;
  }

  // 3. Throttled background check.
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

void UpdateController::consumeStoredResult() {
  auto *service = m_services.get<UpdateService>();
  auto *notifications = m_services.get<NotificationService>();
  if (!service || !notifications) {
    return;
  }

  const auto stored = service->consumeStoredResult();
  if (!stored.isValid()) {
    return;
  }

  NotificationMessage message;
  message.m_title = tr("Update");

  switch (stored.outcome) {
  case UpdateInstaller::ResultOutcome::Applied:
    message.m_severity = NotificationMessage::Severity::Success;
    message.m_duration = NotificationMessage::Duration::Long;
    message.m_text = stored.targetVersion.isEmpty()
                         ? tr("VNote has been updated.")
                         : tr("VNote has been updated to %1.").arg(stored.targetVersion);
    break;

  case UpdateInstaller::ResultOutcome::Retryable:
    message.m_severity = NotificationMessage::Severity::Info;
    message.m_duration = NotificationMessage::Duration::Long;
    message.m_text = tr("The update could not be installed this time and will be retried: %1")
                         .arg(stored.reason);
    break;

  case UpdateInstaller::ResultOutcome::Failed:
    message.m_severity = NotificationMessage::Severity::Warning;
    message.m_duration = NotificationMessage::Duration::Persist;
    message.m_text = tr("The update failed and the previous version was restored: %1")
                         .arg(stored.reason);
    break;

  case UpdateInstaller::ResultOutcome::ManualRecovery:
    message.m_severity = NotificationMessage::Severity::Error;
    message.m_duration = NotificationMessage::Duration::Persist;
    message.m_text =
        tr("The update did not complete and VNote could not restore itself automatically. "
           "See RECOVERY.txt in the '.vnote-old' folder next to VNote. (%1)")
            .arg(stored.reason);
    break;

  case UpdateInstaller::ResultOutcome::SpawnFailed:
    message.m_severity = NotificationMessage::Severity::Warning;
    message.m_duration = NotificationMessage::Duration::Long;
    message.m_text = tr("VNote was updated but could not restart itself automatically.");
    break;

  case UpdateInstaller::ResultOutcome::None:
    return;
  }

  notifications->notify(message);
}

void UpdateController::notifyPendingUpdate(const QString &p_version) {
  auto *notifications = m_services.get<NotificationService>();
  if (!notifications || p_version.isEmpty()) {
    return;
  }

  NotificationMessage message;
  message.m_title = tr("Update Ready");
  message.m_text = tr("VNote %1 has been downloaded and will be installed when VNote closes.")
                       .arg(p_version);
  message.m_severity = NotificationMessage::Severity::Info;
  // Persist: the user must be able to find this action whenever they are ready.
  message.m_duration = NotificationMessage::Duration::Persist;

  NotificationAction restart;
  restart.m_label = tr("Restart to finish update");
  MainWindow2 *window = m_mainWindow;
  restart.m_callback = [window]() {
    if (window) {
      window->restartForUpdate();
    }
  };
  message.m_actions.append(restart);

  notifications->notify(message);
}

// ===========================================================================
// Checking
// ===========================================================================

void UpdateController::checkForUpdatesManually() {
  auto *service = m_services.get<UpdateService>();
  if (!service) {
    openReleasesPage();
    return;
  }

  // A staged update makes a new check pointless; offer to install instead.
  if (service->revalidatePending()) {
    const int answer = MessageBoxHelper::questionYesNo(
        MessageBoxHelper::Question,
        tr("VNote %1 is ready to install.").arg(service->pendingVersion()),
        tr("Restart VNote now to finish the update?"), QString(), m_mainWindow);
    if (answer == QMessageBox::Yes && m_mainWindow) {
      m_mainWindow->restartForUpdate();
    }
    return;
  }

  startCheck(true);
}

void UpdateController::startCheck(bool p_manual) {
  auto *service = m_services.get<UpdateService>();
  if (!service) {
    return;
  }

  m_manualCheck = p_manual;

  // Advance the throttle on check START, not on completion: a failing network
  // must not cause a check on every single launch. Manual checks bypass the
  // throttle entirely but still record the timestamp.
  if (auto *configMgr = m_services.get<ConfigMgr2>()) {
    configMgr->getCoreConfig().setLastUpdateCheckTime(QDateTime::currentMSecsSinceEpoch());
  }

  service->checkForUpdates();
}

void UpdateController::onCheckFinished(const vnotex::UpdateInfo &p_info) {
  if (!p_info.updateAvailable) {
    if (m_manualCheck) {
      showDialog(p_info);
    }
    return;
  }

  if (!m_manualCheck && isVersionSkipped(p_info.latestVersion)) {
    return;
  }

  if (!m_manualCheck) {
    // Silent startup check: surface it as a notification rather than stealing
    // focus with a modal dialog.
    auto *notifications = m_services.get<NotificationService>();
    if (notifications) {
      NotificationMessage message;
      message.m_title = tr("Update Available");
      message.m_text = tr("VNote %1 is available.").arg(p_info.latestVersion);
      message.m_severity = NotificationMessage::Severity::Info;
      message.m_duration = NotificationMessage::Duration::Persist;

      NotificationAction show;
      show.m_label = tr("Details");
      const UpdateInfo info = p_info;
      show.m_callback = [this, info]() { showDialog(info); };
      message.m_actions.append(show);

      notifications->notify(message);
    }
    return;
  }

  showDialog(p_info);
}

void UpdateController::showDialog(const UpdateInfo &p_info) {
  if (m_dialog) {
    m_dialog->raise();
    m_dialog->activateWindow();
    return;
  }

  auto *dialog = new UpdateDialog(p_info, m_mainWindow);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  m_dialog = dialog;

  connect(dialog, &UpdateDialog::downloadRequested, this, [this]() {
    if (auto *service = m_services.get<UpdateService>()) {
      service->startDownload();
    }
  });
  connect(dialog, &UpdateDialog::skipRequested, this,
          [this](const QString &p_version) { skipVersion(p_version); });
  connect(dialog, &UpdateDialog::restartRequested, this, [this]() {
    if (m_mainWindow) {
      m_mainWindow->restartForUpdate();
    }
  });

  dialog->show();
}

void UpdateController::onProgress(const QString &p_stage, qint64 p_done, qint64 p_total) {
  if (m_dialog) {
    m_dialog->setProgress(p_stage, p_done, p_total);
  }
}

void UpdateController::onReadyToApply(const QString &p_version) {
  if (m_dialog) {
    m_dialog->setReadyToApply(p_version);
  }
  notifyPendingUpdate(p_version);
}

void UpdateController::onFailed(const QString &p_message) {
  if (m_dialog) {
    m_dialog->setFailed(p_message);
    return;
  }

  if (!m_manualCheck) {
    // Silent startup check: a failed update check must never interrupt the user.
    qWarning() << "update check failed:" << p_message;
    return;
  }

  MessageBoxHelper::notify(MessageBoxHelper::Warning, tr("Could not check for updates."),
                           p_message, QString(), m_mainWindow);
}

// ===========================================================================
// Quit interaction
// ===========================================================================

bool UpdateController::hasPendingUpdate() const {
  auto *service = m_services.get<UpdateService>();
  return service && service->hasPendingUpdate();
}

bool UpdateController::promptToApplyPendingOnQuit() {
  if (!hasPendingUpdate()) {
    return false;
  }

  // Ask ONCE per session: a close that the user later cancels must not turn
  // into a nagging loop.
  if (m_quitPromptAnswered) {
    return m_quitPromptAccepted;
  }

  auto *service = m_services.get<UpdateService>();
  const int answer = MessageBoxHelper::questionYesNo(
      MessageBoxHelper::Question,
      tr("VNote %1 is ready to install.").arg(service ? service->pendingVersion() : QString()),
      tr("Install it now while VNote closes?"), QString(), m_mainWindow);

  m_quitPromptAnswered = true;
  m_quitPromptAccepted = answer == QMessageBox::Yes;
  return m_quitPromptAccepted;
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
  QDesktopServices::openUrl(QUrl(c_releasesPageUrl));
}
