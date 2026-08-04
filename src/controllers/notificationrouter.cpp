#include "notificationrouter.h"

#include <QDebug>

#include <core/configmgr2.h>
#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/imagehostservice.h>
#include <core/services/notificationservice.h>
#include <core/services/syncservice.h>

#include <imagehost/imagehosttypes.h>

using namespace vnotex;

namespace {

const QString c_categoryBuffer = QStringLiteral("buffer");
const QString c_categorySync = QStringLiteral("sync");
const QString c_categoryImageHost = QStringLiteral("imagehost");
const QString c_categoryViewArea = QStringLiteral("viewarea");
const QString c_categoryConfig = QStringLiteral("config");

QString bufferAutoSaveKey(const QString &p_bufferId) {
  return QStringLiteral("buffer.autosave.%1").arg(p_bufferId);
}

QString bufferSaveErrorKey(const QString &p_bufferId) {
  return QStringLiteral("buffer.saveerror.%1").arg(p_bufferId);
}

QString imageHostKey(int p_token) {
  return QStringLiteral("imagehost.upload.%1").arg(p_token);
}

} // namespace

NotificationRouter::NotificationRouter(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  connectServiceSources();
}

NotificationRouter::~NotificationRouter() {
  if (m_afterStartHookId != -1) {
    // Guard null in case the HookManager was already torn down during shutdown.
    if (auto *hookMgr = m_services.get<HookManager>()) {
      hookMgr->removeAction(m_afterStartHookId);
    }
  }
}

QString NotificationRouter::syncDedupKey(VxCoreError p_code, const QString &p_notebookId) {
  switch (p_code) {
  case VXCORE_ERR_SYNC_AUTH_FAILED:
    return QStringLiteral("sync.auth.%1").arg(p_notebookId);
  case VXCORE_ERR_SYNC_NETWORK:
    return QStringLiteral("sync.network.%1").arg(p_notebookId);
  default:
    return QStringLiteral("sync.failed.%1").arg(p_notebookId);
  }
}

void NotificationRouter::connectServiceSources() {
  // BufferService privately inherits QObject, so it hands out a bare QObject*.
  // That forces the string-based connection form, which is why the buffer slots
  // are named rather than lambdas.
  //
  // A typo in these signatures fails at RUNTIME, silently, and only when the
  // signal would have fired -- i.e. exactly when a user's auto-save is already
  // failing. connect() returns a falsy Connection in that case, so assert it
  // loudly at startup instead.
  if (auto *bufferSvc = m_services.get<BufferService>()) {
    if (auto *obj = bufferSvc->asQObject()) {
      const bool aborted = connect(obj, SIGNAL(bufferAutoSaveAborted(QString)), this,
                                   SLOT(onBufferAutoSaveAborted(QString)));
      const bool failed = connect(obj, SIGNAL(saveError(QString, QString)), this,
                                  SLOT(onBufferSaveError(QString, QString)));
      const bool saved =
          connect(obj, SIGNAL(bufferAutoSaved(QString)), this, SLOT(onBufferAutoSaved(QString)));
      if (!aborted || !failed || !saved) {
        qCritical() << "NotificationRouter: BufferService signal connection failed"
                    << "(aborted:" << aborted << "saveError:" << failed << "saved:" << saved
                    << ") -- buffer failures will not reach the user";
      }
    }
  }

  if (auto *imageSvc = m_services.get<ImageHostService>()) {
    connect(imageSvc, &ImageHostService::uploadFinished, this,
            [this](int p_token, const ImageHostAsyncResult &p_result) {
              auto *notifications = m_services.get<NotificationService>();
              if (!notifications) {
                return;
              }
              if (p_result.success) {
                // Incident over.
                notifications->dismissByDedupKey(imageHostKey(p_token));
                return;
              }

              NotificationMessage msg;
              msg.m_category = c_categoryImageHost;
              // Per-upload token: a second failed upload is a DISTINCT incident
              // and must interrupt, not silently update the first one.
              msg.m_dedupKey = imageHostKey(p_token);
              msg.m_severity = NotificationMessage::Severity::Error;
              msg.m_attention = NotificationMessage::Attention::Interrupt;
              msg.m_duration = NotificationMessage::Duration::Long;
              msg.m_title = tr("Image upload failed");
              msg.m_text = p_result.fileName.isEmpty()
                               ? tr("Failed to upload the image to the image host.")
                               : tr("Failed to upload \"%1\" to the image host.")
                                     .arg(p_result.fileName);
              msg.m_details = p_result.errorMessage;
              // notify(), not renotify(): the token already makes each upload a
              // distinct incident, so this is always a fresh key and therefore
              // always a messageAdded. Using renotify() here would additionally
              // re-interrupt if the same token ever reported twice.
              notifications->notify(msg);
            });
  }

  if (auto *syncSvc = m_services.get<SyncService>()) {
    // Retirement boundaries. Each mirrors a reset that NotebookExplorer2's
    // anti-spam sets used to perform.
    connect(syncSvc, &SyncService::syncFinished, this,
            [this](const QString &p_notebookId, VxCoreError p_result) {
              if (p_result == VXCORE_OK) {
                retireSyncIncidents(p_notebookId);
              }
            });
    connect(syncSvc, &SyncService::enableFinished, this,
            [this](const QString &p_notebookId, VxCoreError p_result, const QString &) {
              if (p_result == VXCORE_OK) {
                retireSyncIncidents(p_notebookId);
              }
            });
    connect(syncSvc, &SyncService::credentialsSetFinished, this,
            [this](const QString &p_notebookId, VxCoreError p_result) {
              if (p_result == VXCORE_OK) {
                retireSyncIncidents(p_notebookId);
              }
            });
    connect(syncSvc, &SyncService::disableFinished, this,
            [this](const QString &p_notebookId, VxCoreError p_result) {
              if (p_result == VXCORE_OK) {
                retireSyncIncidents(p_notebookId);
              }
            });
  }

  if (auto *hookMgr = m_services.get<HookManager>()) {
    // ConfigMgr2 dumps the bundled extra data during main()'s startup, long
    // before this router (or any notification surface) exists. MainWindow2
    // constructs the router in setupNotifications() BEFORE dispatching
    // MainWindowAfterStart, so subscribing here is guaranteed to be in place.
    m_afterStartHookId = hookMgr->addAction(
        HookNames::MainWindowAfterStart,
        [this](HookContext &, const QVariantMap &) { reportExtraDataFailures(); },
        // Late, matching the updater's startup tasks: never compete with
        // session restore for the startup window.
        200);
  }
}

void NotificationRouter::reportExtraDataFailures() {
  auto *notifications = m_services.get<NotificationService>();
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (!notifications || !configMgr) {
    return;
  }

  const auto &failures = configMgr->extraDataCopyFailures();
  for (const auto &failure : failures) {
    NotificationMessage msg;
    msg.m_category = c_categoryConfig;
    // Deliberately KEYLESS. NotificationService is in-memory only, so a dedup
    // key cannot dedup across launches, and this pull happens exactly once per
    // process -- there is nothing to dedup WITHIN a process. A key would also
    // oblige a retirement boundary (src/controllers/AGENTS.md, "Incident
    // retirement is not optional") and there is no in-session success event to
    // retire it at: the retry happens at the NEXT start.
    msg.m_severity = NotificationMessage::Severity::Warning;
    msg.m_attention = NotificationMessage::Attention::Interrupt;
    msg.m_duration = NotificationMessage::Duration::Long;
    msg.m_title = tr("Bundled resources not updated");
    msg.m_text = tr("VNote could not update its bundled \"%1\" data, so some of it may be "
                    "outdated. VNote will try again the next time it starts.")
                     .arg(failure.m_folderName);
    // Rendered only in the popup's collapsible section, which is exactly what
    // a possibly-long list of paths needs.
    QStringList details;
    if (!failure.m_errorMessage.isEmpty()) {
      details.append(failure.m_errorMessage);
    }
    details.append(failure.m_failedPaths);
    msg.m_details = details.join(QLatin1Char('\n'));
    // No action button: there is nothing safe for the user to click.
    notifications->notify(msg);
  }
}

void NotificationRouter::retireSyncIncidents(const QString &p_notebookId) {
  auto *notifications = m_services.get<NotificationService>();
  if (!notifications || p_notebookId.isEmpty()) {
    return;
  }
  notifications->dismissByDedupKey(
      syncDedupKey(VXCORE_ERR_SYNC_AUTH_FAILED, p_notebookId));
  notifications->dismissByDedupKey(syncDedupKey(VXCORE_ERR_SYNC_NETWORK, p_notebookId));
  notifications->dismissByDedupKey(syncDedupKey(VXCORE_ERR_UNKNOWN, p_notebookId));
}

void NotificationRouter::onSyncUserMessageRequested(const QString &p_notebookId,
                                                    VxCoreError p_code, const QString &p_title,
                                                    const QString &p_text,
                                                    const QString &p_details) {
  auto *notifications = m_services.get<NotificationService>();
  if (!notifications) {
    return;
  }

  NotificationMessage msg;
  msg.m_category = c_categorySync;
  msg.m_dedupKey = syncDedupKey(p_code, p_notebookId);
  msg.m_title = p_title;
  msg.m_text = p_text;
  // The long backend message (HTTP body, libgit2 detail) used to be a
  // QMessageBox detailedText; it now lives in the popup's collapsible section.
  msg.m_details = p_details;

  switch (p_code) {
  case VXCORE_ERR_SYNC_AUTH_FAILED:
    msg.m_severity = NotificationMessage::Severity::Warning;
    msg.m_attention = NotificationMessage::Attention::Interrupt;
    msg.m_duration = NotificationMessage::Duration::Persist;
    break;
  case VXCORE_ERR_SYNC_NETWORK:
    // Transient and self-healing: VNote retries on the next change, so this
    // does not deserve to interrupt.
    msg.m_severity = NotificationMessage::Severity::Info;
    msg.m_attention = NotificationMessage::Attention::Passive;
    msg.m_duration = NotificationMessage::Duration::Long;
    break;
  default:
    msg.m_severity = NotificationMessage::Severity::Warning;
    msg.m_attention = NotificationMessage::Attention::Interrupt;
    msg.m_duration = NotificationMessage::Duration::Persist;
    break;
  }

  if (p_code == VXCORE_ERR_SYNC_AUTH_FAILED) {
    NotificationAction openInfo;
    openInfo.m_label = tr("Open Sync Info...");
    openInfo.m_dismissOnTrigger = true;
    const QString notebookId = p_notebookId;
    openInfo.m_callback = [this, notebookId]() { emit openSyncInfoRequested(notebookId); };
    msg.m_actions.append(openInfo);
  }

  // Plain notify(): a repeat failure of the SAME incident folds into the live
  // message and stays quiet. It only interrupts again after a retirement
  // boundary (or a manual retry) has retired the key.
  notifications->notify(msg);
}

void NotificationRouter::onSyncIncidentRetryRequested(const QString &p_notebookId) {
  retireSyncIncidents(p_notebookId);
}

void NotificationRouter::onViewWindowCreationFailed(const QString &p_fileType,
                                                    const QString &p_path) {
  auto *notifications = m_services.get<NotificationService>();
  if (!notifications) {
    return;
  }

  NotificationMessage msg;
  msg.m_category = c_categoryViewArea;
  // Deliberately KEYLESS. Opening a file is user-initiated, so every attempt
  // must give feedback; a dedup key would silence the second double-click, and
  // there is no natural boundary at which to retire it.
  msg.m_severity = NotificationMessage::Severity::Warning;
  msg.m_attention = NotificationMessage::Attention::Interrupt;
  msg.m_duration = NotificationMessage::Duration::Long;
  msg.m_title = tr("Cannot open file");
  msg.m_text = p_path.isEmpty()
                   ? tr("No viewer is available for file type \"%1\".").arg(p_fileType)
                   : tr("No viewer is available for \"%1\" (type \"%2\").")
                         .arg(p_path, p_fileType);
  notifications->notify(msg);
}

void NotificationRouter::onBufferAutoSaveAborted(const QString &p_bufferId) {
  auto *notifications = m_services.get<NotificationService>();
  if (!notifications) {
    return;
  }

  NotificationMessage msg;
  msg.m_category = c_categoryBuffer;
  msg.m_dedupKey = bufferAutoSaveKey(p_bufferId);
  msg.m_severity = NotificationMessage::Severity::Error;
  // Auto-save has GIVEN UP. The user's work is only in memory, so this has
  // earned the interruption.
  msg.m_attention = NotificationMessage::Attention::Interrupt;
  msg.m_duration = NotificationMessage::Duration::Persist;
  msg.m_title = tr("Auto-save stopped");
  msg.m_text = tr("VNote stopped trying to auto-save this note after repeated failures. "
                  "Your changes are only in memory -- save manually to a different "
                  "location to avoid losing them.");
  // notify(), NOT renotify(): BufferService re-emits bufferAutoSaveAborted on
  // every subsequent attempt once the failure count is past the threshold, so
  // renotify() would remove-and-repost each time and re-raise the toast on a
  // loop. Folding keeps the incident quiet until onBufferAutoSaved() retires
  // it, which is what makes the NEXT genuine failure interrupt again.
  notifications->notify(msg);
}

void NotificationRouter::onBufferSaveError(const QString &p_bufferId, const QString &p_errorMsg) {
  auto *notifications = m_services.get<NotificationService>();
  if (!notifications) {
    return;
  }

  NotificationMessage msg;
  msg.m_category = c_categoryBuffer;
  msg.m_dedupKey = bufferSaveErrorKey(p_bufferId);
  msg.m_severity = NotificationMessage::Severity::Warning;
  // Auto-save will retry on the next tick, so a single failure is informational.
  // The abort above is the one that interrupts.
  msg.m_attention = NotificationMessage::Attention::Passive;
  msg.m_duration = NotificationMessage::Duration::Long;
  msg.m_title = tr("Auto-save failed");
  msg.m_text = tr("Could not auto-save this note. VNote will retry.");
  msg.m_details = p_errorMsg;
  notifications->notify(msg);
}

void NotificationRouter::onBufferAutoSaved(const QString &p_bufferId) {
  auto *notifications = m_services.get<NotificationService>();
  if (!notifications) {
    return;
  }
  // Incident over: the next failure is a new one and may interrupt again.
  notifications->dismissByDedupKey(bufferAutoSaveKey(p_bufferId));
  notifications->dismissByDedupKey(bufferSaveErrorKey(p_bufferId));
}
