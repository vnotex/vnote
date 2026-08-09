#include "updatecontroller.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QSslSocket>
#include <QUrl>

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

} // namespace

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
    message.m_text = tr("VNote %1 is available. Open the release page to download it.")
                         .arg(p_info.latestVersion);
    message.m_severity = NotificationMessage::Severity::Info;
    // Persist: the user must be able to find this whenever they are ready.
    message.m_duration = NotificationMessage::Duration::Persist;
    message.m_actions.append(makeCheckReleaseAction(p_info));
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
