#ifndef UPDATECONTROLLER_H
#define UPDATECONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QString>

#include <core/services/notificationservice.h>
#include <core/services/updateservice.h>

class QWidget;

namespace vnotex {

class ServiceLocator;
class UpdateDialog;

// Mediates between UpdateService (mechanism) and the update UI (view).
//
// All UPDATE POLICY lives here, not in the service:
//   * the "check for updates on start" flag and its 24 h throttle;
//   * the skipped-version filter;
//   * the configured release source, pushed down into the service;
//   * whether a failure is reported loudly (manual check) or silently
//     (startup check).
//
// The only thing offered when an update exists is the release page: VNote does
// not download anything. It is a QObject, NOT a QWidget, so it stays testable
// without a GUI; the only widget it needs is a parent for the dialog and
// message boxes it opens.
class UpdateController : public QObject {
  Q_OBJECT

public:
  UpdateController(ServiceLocator &p_services, QWidget *p_parentWidget,
                   QObject *p_parent = nullptr);
  ~UpdateController() override;

  // Runs once after MainWindowAfterStart: applies the configured source and
  // starts a throttled background check when enabled.
  void runStartupTasks();

public slots:
  // Menu entry. Always reports its outcome and is never throttled.
  void checkForUpdatesManually();

private slots:
  void onCheckFinished(const vnotex::UpdateInfo &p_info);
  void onFailed(const QString &p_message);

private:
  // Starts a check owned by this controller. Returns whether one was actually
  // started; a request made while another check is still in flight is refused
  // WITHOUT touching m_manualCheck.
  bool startCheck(bool p_manual);

  void openReleasesPage() const;

  // Pushes the configured release source into UpdateService. Called from the
  // constructor and again at the top of every check, so a Settings change takes
  // effect without a restart.
  void applyConfiguredSource();

  bool isVersionSkipped(const QString &p_version) const;
  void skipVersion(const QString &p_version);

  void showDialog(const UpdateInfo &p_info);

  // Action that opens the release page of @p_info, falling back to the current
  // source's releases page.
  NotificationAction makeCheckReleaseAction(const UpdateInfo &p_info) const;

  // Drops the tracked offer notification. A new check supersedes whatever the
  // previous one advertised, so its button must not outlive it.
  void invalidateTrackedNotification();

  ServiceLocator &m_services;

  // Dialog / message-box parent only. The controller has no other reason to
  // know about the widget layer.
  QWidget *m_parentWidget = nullptr;

  QPointer<UpdateDialog> m_dialog;

  // Which request the CURRENTLY IN-FLIGHT check came from. Set only for a
  // request UpdateService actually accepted, and read only by the terminal
  // slots. Assuming a request landed would let a DROPPED manual check re-label
  // the running startup check's outcome -- turning a silent background failure
  // into a modal warning box.
  bool m_manualCheck = false;

  // True from an accepted checkForUpdates() until its terminal slot runs. The
  // service releases its own busy flag before the queued terminal signal is
  // DELIVERED, so this flag -- not the service's -- is what keeps one check's
  // result from being interpreted with the next check's mode.
  bool m_checkInFlight = false;

  // The notification carrying the current offer, 0 when there is none. Assigned
  // ONLY from calls using the "update.available" key.
  quint64 m_offerNotificationId = 0;
};

} // namespace vnotex

#endif // UPDATECONTROLLER_H
