#ifndef UPDATECONTROLLER_H
#define UPDATECONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QString>

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

  void notifyPendingUpdate(const QString &p_version);

  void consumeStoredResult();

  bool isVersionSkipped(const QString &p_version) const;
  void skipVersion(const QString &p_version);

  void showDialog(const UpdateInfo &p_info);

  ServiceLocator &m_services;
  MainWindow2 *m_mainWindow = nullptr;

  QPointer<UpdateDialog> m_dialog;

  // Distinguishes the menu-driven check from the silent startup one.
  bool m_manualCheck = false;

  // Set once the user has answered the quit prompt, so a cancelled close does
  // not ask again on the next attempt within the same session.
  bool m_quitPromptAnswered = false;
  bool m_quitPromptAccepted = false;
};

} // namespace vnotex

#endif // UPDATECONTROLLER_H
