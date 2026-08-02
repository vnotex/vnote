#ifndef UPDATECONTROLLER_H
#define UPDATECONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QString>

#include <core/services/notificationservice.h>
#include <core/services/updateservice.h>

namespace vnotex {

class ServiceLocator;
class UpdateDialog;
class MainWindow2;

// Mediates between UpdateService (mechanism) and the update UI (view).
//
// All UPDATE POLICY lives here, not in the service:
//   * the "check for updates on start" flag and its 24 h throttle;
//   * the skipped-version filter;
//   * whether a failure is reported loudly (manual check) or silently
//     (startup check);
//   * turning a persisted apply outcome into a notification after the restart;
//   * the "restart to finish the update?" prompt on a normal quit.
//
// It is a QObject, NOT a QWidget, so it stays testable without a GUI.
class UpdateController : public QObject {
  Q_OBJECT

public:
  UpdateController(ServiceLocator &p_services, MainWindow2 *p_mainWindow,
                   QObject *p_parent = nullptr);
  ~UpdateController() override;

  // Runs once after MainWindowAfterStart:
  //   1. turns a persisted result.json into a notification;
  //   2. revalidates any pending update and offers "Restart to finish";
  //   3. starts a throttled background check when enabled.
  void runStartupTasks();

  // True when a validated staged update is waiting to be applied at quit.
  bool hasPendingUpdate() const;

  // Called from MainWindow2's close path. Returns true when the user accepted
  // applying the pending update (the caller then quits with
  // kExitToApplyUpdate); false to quit normally.
  bool promptToApplyPendingOnQuit();

public slots:
  // Menu entry. Always reports its outcome, never throttled, and falls back to
  // opening the releases page when this install is not eligible.
  void checkForUpdatesManually();

private slots:
  void onCheckFinished(const vnotex::UpdateInfo &p_info);
  void onProgress(const QString &p_stage, qint64 p_done, qint64 p_total);
  void onReadyToApply(const QString &p_version);
  void onFailed(const QString &p_message);

private:
  void startCheck(bool p_manual);

  void openReleasesPage() const;

  // Pushes the configured release source into UpdateService. Called from the
  // constructor and again at the top of every check, so a Settings change takes
  // effect without a restart.
  void applyConfiguredSource();

  void notifyPendingUpdate(const QString &p_version);

  void consumeStoredResult();

  bool isVersionSkipped(const QString &p_version) const;
  void skipVersion(const QString &p_version);

  void showDialog(const UpdateInfo &p_info);

  // Starts a download that reports into the notification (never the dialog) and
  // switches the tracked message to its in-progress state. Used by both the
  // Update and the Retry actions.
  void startNotificationDownload();

  // Posts p_msg as the tracked transfer notification, tagging it with the
  // "update.transfer" dedup key. The service call is derived from
  // p_msg.m_attention: Interrupt uses renotify() (so a terminal state always
  // reaches the toast, even when no passive phase preceded it), Passive uses
  // notify() (so progress folds into one row).
  void postTransferNotification(const NotificationMessage &p_msg);

  // Action that opens the release page of @p_info, falling back to the current
  // source's releases page. Takes the info explicitly: the dialog and the
  // notification can be describing different checks.
  NotificationAction makeCheckReleaseAction(const UpdateInfo &p_info) const;

  // Drops the tracked offer/retry notification. A new check invalidates the
  // plan those actions would start, so their buttons must not outlive it.
  void invalidateTrackedNotification();

  // Which surface the CURRENT transfer reports into. Routing must not key off
  // m_dialog / m_manualCheck: those describe the last CHECK, and a startup
  // notification can coexist with an open non-modal UpdateDialog.
  enum class TransferSurface { None, Dialog, Notification };

  ServiceLocator &m_services;
  MainWindow2 *m_mainWindow = nullptr;

  QPointer<UpdateDialog> m_dialog;

  // Distinguishes the menu-driven check from the silent startup one.
  bool m_manualCheck = false;

  // Set at the exact action that calls startDownload(), reset on every terminal
  // outcome.
  TransferSurface m_transfer = TransferSurface::None;

  // The notification carrying the offer, then the progress, then the terminal
  // state. 0 when there is none.
  //
  // Assigned ONLY from calls using the "update.transfer" key. The post-restart
  // apply result ("update.result") and the standalone dialog-closed failure
  // (keyless) must never land here: startCheck() dismisses this id, and
  // runStartupTasks() consumes the stored result BEFORE startCheck(), so
  // sharing it would silently eat the apply outcome.
  quint64 m_progressNotificationId = 0;

  // The version currently being offered / downloaded through the notification.
  UpdateInfo m_offeredInfo;

  // The version the currently open (or last opened) UpdateDialog describes.
  // Kept separate from m_offeredInfo: a startup notification and a manual
  // dialog can be about different checks.
  UpdateInfo m_dialogInfo;

  // Coalescing state for the notification progress bar. Every update() rebuilds
  // the popup, and QNetworkReply::downloadProgress fires far too often to do
  // that per tick, so a tick is forwarded only when the whole-percent bucket or
  // the stage text actually changes. -1 means "nothing sent yet".
  int m_lastProgressBucket = -1;
  QString m_lastProgressStage;

  // Set once the user has answered the quit prompt, so a cancelled close does
  // not ask again on the next attempt within the same session.
  bool m_quitPromptAnswered = false;
  bool m_quitPromptAccepted = false;
};

} // namespace vnotex

#endif // UPDATECONTROLLER_H
