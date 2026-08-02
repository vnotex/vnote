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

const QString c_updateCategory = QStringLiteral("update");

// One incident per transfer: offer -> download -> progress -> terminal state all
// fold into a single notification. The dialog and notification surfaces can
// share this key because TransferSurface already guarantees only one transfer
// is live at a time.
const QString c_transferDedupKey = QStringLiteral("update.transfer");

// The post-restart apply outcome is a SEPARATE incident from a transfer, and
// must never be assigned to m_progressNotificationId: runStartupTasks() consumes
// the stored result BEFORE startCheck(), and startCheck() dismisses the tracked
// notification -- so sharing the id would silently eat the apply result.
const QString c_resultDedupKey = QStringLiteral("update.result");

} // namespace

UpdateController::UpdateController(ServiceLocator &p_services, MainWindow2 *p_mainWindow,
                                   QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_mainWindow(p_mainWindow) {
  auto *service = m_services.get<UpdateService>();
  if (!service) {
    return;
  }

  applyConfiguredSource();

  connect(service, &UpdateService::checkFinished, this, &UpdateController::onCheckFinished);
  connect(service, &UpdateService::progress, this, &UpdateController::onProgress);
  connect(service, &UpdateService::readyToApply, this, &UpdateController::onReadyToApply);
  connect(service, &UpdateService::failed, this, &UpdateController::onFailed);

  // A tracked message can leave the store without being dismissed: the
  // retention cap can evict it. Without this, m_progressNotificationId would
  // keep naming a message that no longer exists.
  if (auto *notifications = m_services.get<NotificationService>()) {
    connect(notifications, &NotificationService::messageRemoved, this, [this](quint64 p_id) {
      if (m_progressNotificationId != 0 && p_id == m_progressNotificationId) {
        m_progressNotificationId = 0;
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
    message.m_text =
        tr("The update failed and the previous version was restored: %1").arg(stored.reason);
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

  message.m_category = c_updateCategory;
  message.m_dedupKey = c_resultDedupKey;
  // The outcome of an update the user already committed to: worth interrupting.
  message.m_attention = NotificationMessage::Attention::Interrupt;
  // NOT tracked in m_progressNotificationId -- see c_resultDedupKey.
  notifications->renotify(message);
}

void UpdateController::notifyPendingUpdate(const QString &p_version) {
  auto *notifications = m_services.get<NotificationService>();
  if (!notifications || p_version.isEmpty()) {
    return;
  }

  NotificationMessage message;
  message.m_title = tr("Update Ready");
  message.m_text =
      tr("VNote %1 has been downloaded and will be installed when VNote closes.").arg(p_version);
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

  // Terminal ready-to-install state. Reached from BOTH the startup pending-update
  // path AND a dialog-owned transfer that just completed, so a live transfer
  // message may already hold the key -- postTransferNotification's renotify() is
  // what keeps it from being folded in silently.
  message.m_attention = NotificationMessage::Attention::Interrupt;
  postTransferNotification(message);
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

  // A check replaces the plan, so any Update/Retry button still sitting in the
  // notification list would start something other than what it advertises.
  // Drop it here, in ONE place, rather than trying to reconcile it afterwards.
  invalidateTrackedNotification();

  // Pick up a Settings change without a restart. Ignored by the service while
  // an operation is in flight, which is the correct behavior.
  applyConfiguredSource();

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
      m_offeredInfo = p_info;

      NotificationMessage message;
      message.m_title = tr("Update Available");
      message.m_text = tr("VNote %1 is available.").arg(p_info.latestVersion);
      message.m_severity = NotificationMessage::Severity::Info;
      message.m_duration = NotificationMessage::Duration::Persist;

      if (p_info.eligible) {
        // Only offered when an in-place update is actually possible: false off
        // Windows, for Microsoft Store installs, and for a source that
        // publishes no manifest for this release.
        NotificationAction update;
        update.m_label = tr("Update");
        // The message must survive the click so it can carry the progress bar.
        update.m_dismissOnTrigger = false;
        update.m_callback = [this]() { startNotificationDownload(); };
        message.m_actions.append(update);
      }

      message.m_actions.append(makeCheckReleaseAction(p_info));

      // A brand-new offer the user has not seen: this earns the interruption.
      message.m_attention = NotificationMessage::Attention::Interrupt;
      postTransferNotification(message);
    }
    return;
  }

  showDialog(p_info);
}

void UpdateController::invalidateTrackedNotification() {
  if (m_progressNotificationId == 0) {
    return;
  }
  if (m_transfer == TransferSurface::Notification) {
    // The tracked message is currently rendering a live transfer. Dismissing it
    // would orphan the progress ticks and the terminal state; the service will
    // refuse the concurrent check anyway.
    return;
  }
  if (auto *notifications = m_services.get<NotificationService>()) {
    notifications->dismiss(m_progressNotificationId);
  }
  m_progressNotificationId = 0;
  m_offeredInfo = UpdateInfo();
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

void UpdateController::startNotificationDownload() {
  auto *service = m_services.get<UpdateService>();
  auto *notifications = m_services.get<NotificationService>();
  if (!service || !notifications) {
    return;
  }

  // One transfer at a time. Claiming a surface for a request the service is
  // about to refuse would either strand m_transfer forever (a Busy refusal
  // produces no terminal signal) or hand an already-running transfer's result
  // to the wrong surface.
  if (m_transfer != TransferSurface::None) {
    qWarning() << "update: a transfer is already in flight; ignoring the request";
    return;
  }

  const auto started = service->startDownload(m_offeredInfo.latestVersion);
  if (started == UpdateService::DownloadStart::Busy) {
    // Nothing started and nothing will be emitted: leave every surface alone.
    return;
  }
  if (started == UpdateService::DownloadStart::Stale) {
    // The offer this button came from has been superseded. Say so instead of
    // doing nothing; startCheck() normally dismisses such a message first, so
    // this is the belt-and-braces path.
    NotificationMessage message;
    message.m_title = tr("Update");
    message.m_text = tr("This update offer is out of date. Please check for updates again.");
    message.m_severity = NotificationMessage::Severity::Warning;
    message.m_duration = NotificationMessage::Duration::Persist;
    message.m_actions.append(makeCheckReleaseAction(m_offeredInfo));
    // The user just clicked Update and deserves an explanation; a Passive
    // message here would silently hide the toast they were looking at.
    message.m_attention = NotificationMessage::Attention::Interrupt;
    postTransferNotification(message);
    return;
  }
  if (started == UpdateService::DownloadStart::NoPlan) {
    // failed() IS emitted for this call, so the notification does own the
    // outcome and must be claimed to receive it.
    m_transfer = TransferSurface::Notification;
    return;
  }

  m_transfer = TransferSurface::Notification;
  m_lastProgressBucket = -1;
  m_lastProgressStage.clear();

  NotificationMessage message;
  message.m_title = tr("Update");
  message.m_text = tr("Downloading VNote %1...").arg(m_offeredInfo.latestVersion);
  message.m_severity = NotificationMessage::Severity::Info;
  message.m_duration = NotificationMessage::Duration::Persist;
  message.m_progressIndeterminate = true;

  NotificationAction cancel;
  cancel.m_label = tr("Cancel");
  cancel.m_dismissOnTrigger = false;
  cancel.m_callback = [this]() {
    if (auto *svc = m_services.get<UpdateService>()) {
      svc->cancel();
    }
  };
  message.m_actions.append(cancel);

  // Passive: the user just asked for this, so it is not news. It is also what
  // retires the offer toast -- the toast hides when the message it is showing
  // is updated to Passive.
  message.m_attention = NotificationMessage::Attention::Passive;
  postTransferNotification(message);
}

void UpdateController::postTransferNotification(const NotificationMessage &p_msg) {
  auto *notifications = m_services.get<NotificationService>();
  if (!notifications) {
    return;
  }

  NotificationMessage msg = p_msg;
  msg.m_category = c_updateCategory;
  msg.m_dedupKey = c_transferDedupKey;

  // The call is derived from the attention so a site cannot get it wrong.
  //
  // Interrupt -> renotify(): the toast is raised ONLY by
  // messageAdded(Interrupt), so folding an interrupting terminal state into the
  // live (passive) progress message would emit messageUpdated and never be
  // seen. renotify() removes the old generation and posts a new one, which also
  // makes this correct for the DownloadStart::NoPlan path -- there the offer
  // goes straight to a failure with no passive phase in between.
  //
  // Passive -> notify(): folds into the live message, so the transfer stays one
  // row in the list. When the toast is currently showing that message, the
  // resulting messageUpdated(Passive) is also what retires it.
  if (msg.m_attention == NotificationMessage::Attention::Interrupt) {
    m_progressNotificationId = notifications->renotify(msg);
  } else {
    m_progressNotificationId = notifications->notify(msg);
  }
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
  m_dialogInfo = p_info;

  connect(dialog, &UpdateDialog::downloadRequested, this, [this]() {
    auto *service = m_services.get<UpdateService>();
    if (!service) {
      return;
    }

    // UpdateDialog calls setDownloading() right AFTER emitting this signal, so
    // a refusal reported inline would immediately be overwritten. Report it
    // through the event loop instead.
    auto refuse = [this](const QString &p_why) {
      QMetaObject::invokeMethod(
          this,
          [this, p_why]() {
            if (m_dialog) {
              m_dialog->setFailed(p_why);
            }
          },
          Qt::QueuedConnection);
    };

    // Claim the surface only for a request the service actually accepted, so a
    // refusal can neither strand m_transfer (a Busy refusal emits nothing) nor
    // steal another surface's in-flight result.
    if (m_transfer != TransferSurface::None) {
      refuse(tr("Another update operation is already running."));
      return;
    }

    // The expected version pins this dialog to the plan it was built from: a
    // check that landed while the dialog sat open must not be downloaded from
    // a button that still advertises the old version.
    const auto started = service->startDownload(m_dialogInfo.latestVersion);
    if (started == UpdateService::DownloadStart::Busy) {
      refuse(tr("Another update operation is already running."));
      return;
    }
    if (started == UpdateService::DownloadStart::Stale) {
      refuse(tr("This update offer is out of date. Please check for updates again."));
      return;
    }

    // Started AND NoPlan both own a terminal signal for this call.
    m_transfer = TransferSurface::Dialog;
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
  switch (m_transfer) {
  case TransferSurface::Dialog:
    if (m_dialog) {
      m_dialog->setProgress(p_stage, p_done, p_total);
    }
    return;

  case TransferSurface::Notification: {
    auto *notifications = m_services.get<NotificationService>();
    if (!notifications || m_progressNotificationId == 0 ||
        !notifications->isActive(m_progressNotificationId)) {
      // A progress tick for a dismissed message is simply dropped; only
      // terminal states are worth reposting.
      return;
    }

    // -1 for the indeterminate case, else whole percent. Coalescing on this
    // bucket bounds the popup rebuilds at ~100 per stage instead of one per
    // downloadProgress signal.
    const int permille = p_total > 0 ? static_cast<int>((p_done * 1000) / p_total) : -1;
    const int bucket = permille < 0 ? -1 : permille / 10;
    if (bucket == m_lastProgressBucket && p_stage == m_lastProgressStage) {
      return;
    }
    m_lastProgressBucket = bucket;
    m_lastProgressStage = p_stage;

    NotificationMessage message;
    message.m_title = tr("Update");
    message.m_text = p_stage;
    message.m_severity = NotificationMessage::Severity::Info;
    message.m_duration = NotificationMessage::Duration::Persist;
    // Explicit: a progress tick must never interrupt. (update() replaces the
    // attention field, so relying on the default would be implicit.)
    message.m_attention = NotificationMessage::Attention::Passive;
    if (permille >= 0) {
      message.m_progressPermille = permille;
    } else {
      message.m_progressIndeterminate = true;
    }

    NotificationAction cancel;
    cancel.m_label = tr("Cancel");
    cancel.m_dismissOnTrigger = false;
    cancel.m_callback = [this]() {
      if (auto *svc = m_services.get<UpdateService>()) {
        svc->cancel();
      }
    };
    message.m_actions.append(cancel);

    notifications->update(m_progressNotificationId, message);
    return;
  }

  case TransferSurface::None:
  default:
    return;
  }
}

void UpdateController::onReadyToApply(const QString &p_version) {
  const TransferSurface surface = m_transfer;
  m_transfer = TransferSurface::None;

  if (surface == TransferSurface::Notification) {
    NotificationMessage message;
    message.m_title = tr("Update Ready");
    message.m_text =
        tr("VNote %1 has been downloaded and will be installed when VNote closes.").arg(p_version);
    message.m_severity = NotificationMessage::Severity::Success;
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

    // The same message becomes the success state, so notifyPendingUpdate() is
    // deliberately NOT called here: two identical notifications would be worse
    // than one.
    message.m_attention = NotificationMessage::Attention::Interrupt;
    postTransferNotification(message);
    return;
  }

  if (m_dialog) {
    m_dialog->setReadyToApply(p_version);
  }
  notifyPendingUpdate(p_version);
}

void UpdateController::onFailed(const QString &p_message) {
  const TransferSurface surface = m_transfer;
  m_transfer = TransferSurface::None;

  if (surface == TransferSurface::Notification) {
    NotificationMessage message;
    message.m_title = tr("Update");
    message.m_text = tr("The update failed: %1").arg(p_message);
    message.m_severity = NotificationMessage::Severity::Error;
    message.m_duration = NotificationMessage::Duration::Persist;

    NotificationAction retry;
    retry.m_label = tr("Retry");
    retry.m_dismissOnTrigger = false;
    retry.m_callback = [this]() { startNotificationDownload(); };
    message.m_actions.append(retry);

    message.m_actions.append(makeCheckReleaseAction(m_offeredInfo));

    // Terminal failure of a transfer the user started. Interrupt via
    // renotify(), which also covers the DownloadStart::NoPlan and
    // Retry -> NoPlan paths, where the message goes from an interrupting offer
    // straight to an interrupting failure with no passive phase between them.
    message.m_attention = NotificationMessage::Attention::Interrupt;
    postTransferNotification(message);
    return;
  }

  if (surface == TransferSurface::Dialog) {
    if (m_dialog) {
      m_dialog->setFailed(p_message);
      return;
    }
    // The dialog was closed mid-transfer. The outcome is a DOWNLOAD failure,
    // not a check failure, so it must not be reported as "could not check for
    // updates". Post a standalone notification instead of reusing the tracked
    // one, which may still be carrying an unrelated startup offer -- and link
    // it to the DIALOG's release, not to that offer's.
    if (auto *notifications = m_services.get<NotificationService>()) {
      NotificationMessage message;
      message.m_title = tr("Update");
      message.m_text = tr("The update failed: %1").arg(p_message);
      message.m_severity = NotificationMessage::Severity::Error;
      message.m_duration = NotificationMessage::Duration::Persist;
      message.m_actions.append(makeCheckReleaseAction(m_dialogInfo));
      // Deliberately KEYLESS and untracked: this failure is standalone, must not
      // fold into (or replace) the tracked transfer message, and being keyless
      // means it always arrives as messageAdded and therefore always interrupts.
      message.m_category = c_updateCategory;
      message.m_attention = NotificationMessage::Attention::Interrupt;
      notifications->notify(message);
    } else {
      qWarning() << "update download failed:" << p_message;
    }

    return;
  }

  // No transfer was in flight: this is a CHECK failure.
  if (m_dialog) {
    m_dialog->setFailed(p_message);
    return;
  }

  if (!m_manualCheck) {
    // Silent startup check: a failed update check must never interrupt the user.
    qWarning() << "update check failed:" << p_message;
    return;
  }

  MessageBoxHelper::notify(MessageBoxHelper::Warning, tr("Could not check for updates."), p_message,
                           QString(), m_mainWindow);
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
  if (auto *service = m_services.get<UpdateService>()) {
    QDesktopServices::openUrl(service->releasesPageUrl());
    return;
  }
  QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/vnotex/vnote/releases")));
}
