#ifndef OPENNOTEBOOKDIALOG2_H
#define OPENNOTEBOOKDIALOG2_H

#include "scrolldialog.h"

#include <QString>

class QButtonGroup;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QStackedWidget;

namespace vnotex {

class LocationInputWithBrowseButton;
class OpenNotebookController;
class ServiceLocator;
struct CloneAndOpenResult;

// OpenNotebookDialog2 - View for opening an existing notebook.
// Two modes:
//   * Local Folder: pick a root folder on disk via LocationInputWithBrowseButton
//     and open it through OpenNotebookController::openNotebook().
//   * Remote URL  : provide an HTTPS or file:// URL, an optional PAT (empty
//     means read-only), and a local root folder. The local root folder may
//     either NOT exist yet (the controller creates it) OR be an existing
//     empty directory. Clone wiring drives
//     OpenNotebookController::cloneAndOpen and exposes mid-clone Cancel via
//     OpenNotebookController::cancelClone.
//
// Pure UI: all business logic is delegated to OpenNotebookController. The
// dialog never calls vxcore directly and never touches the credentials store.
//
// On a successful Local-mode open, the dialog emits notebookOpened(QString)
// with the new notebook ID and accept()s. Remote mode wires the controller
// signals (cloneFinished + cloneProgressUpdated) and emits notebookOpened +
// accept() on a successful clone result.
//
// Banner-suppression contract (see plan refine-open-notebook-dialog):
// changes to the URL or PAT fields NEVER set the ScrollDialog info-text
// banner; they only flip the Open button enabled state. Only changes to
// the Local root folder (when non-empty AND invalid) surface a banner
// message. Clone start / progress / failure events continue to use the
// banner. This keeps the dialog quiet and stable in size while typing.
class OpenNotebookDialog2 : public ScrollDialog {
  Q_OBJECT

public:
  enum Mode { LocalMode = 0, RemoteMode = 1 };

  explicit OpenNotebookDialog2(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~OpenNotebookDialog2() override;

  // Returns the ID of the freshly opened notebook (valid after accept()).
  QString getOpenedNotebookId() const;

  Mode currentMode() const;

signals:
  // Emitted on a successful open. p_suppressSyncPrompt is true ONLY for a
  // no-PAT remote clone that landed as writable S2 and must open silently
  // (no PAT auto-prompt). False for all normal opens (local + with-PAT clone).
  void notebookOpened(const QString &p_notebookId, bool p_suppressSyncPrompt);

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

  // openurl-followups Item 2: intercept Cancel clicks so an in-flight clone
  // can be aborted via OpenNotebookController::cancelClone instead of
  // closing the dialog. The base class default (reject()) runs ONLY when
  // no clone is in flight.
  void rejectedButtonClicked() Q_DECL_OVERRIDE;

private slots:
  void onModeChanged();
  void onLocalRootChanged();
  void onRemoteFieldsChanged();

  // openurl-followups Item 2: controller signal handlers wired in setupUI.
  // Both bounce on the GUI thread (default Qt::AutoConnection across same-
  // thread sender/receiver — but the controller emits via QueuedConnection
  // from its worker tail, so we receive on the GUI thread either way).
  void onCloneProgressUpdated(int p_current, int p_total, const QString &p_phase);
  void onCloneFinished(const CloneAndOpenResult &p_result);

private:
  void setupUI();
  void setupLocalPage(QWidget *p_page);
  void setupRemotePage(QWidget *p_page);

  // Recompute the Open button enabled state from the active mode's validation.
  void updateOpenButtonState();

  // Remote-mode validation result. See validateRemoteInputs() for details.
  // surfaceInBanner controls whether the validation message is shown in the
  // ScrollDialog info-text banner (true) or only used to disable the Open
  // button silently (false). URL / PAT errors are silent; Local root folder
  // errors surface in the banner. Per plan refine-open-notebook-dialog.
  struct RemoteValidation {
    bool valid = false;
    bool surfaceInBanner = false;
    QString message;
  };
  RemoteValidation validateRemoteInputs() const;

  // Local-mode Open click: synchronous controller call + signal + accept().
  void handleLocalOpen();

  // openurl-followups Item 2: Remote-mode Open click. Builds the
  // CloneAndOpenInput from the URL/PAT/dest fields, disables the mode-toggle
  // radios + Open button + relevant inputs, shows the indeterminate progress
  // bar, sets the "Cloning..." info text, flips m_cloneInProgress=true, and
  // dispatches OpenNotebookController::cloneAndOpen. Completion arrives via
  // onCloneFinished on the GUI thread; progress beats arrive via
  // onCloneProgressUpdated.
  void handleRemoteOpen();

  // Helper: toggle the enabled state of remote-mode inputs as a single
  // block. Used to disable inputs during a clone and re-enable them on
  // failure / cancellation so the user can retry without dismissing the
  // dialog.
  void setRemoteInputsEnabled(bool p_enabled);

  ServiceLocator &m_services;

  OpenNotebookController *m_controller = nullptr;

  // Top mode selector.
  QRadioButton *m_localModeRadio = nullptr;
  QRadioButton *m_remoteModeRadio = nullptr;
  QButtonGroup *m_modeGroup = nullptr;
  QStackedWidget *m_modeStack = nullptr;

  // Local-mode page.
  QWidget *m_localPage = nullptr;
  LocationInputWithBrowseButton *m_localRootInput = nullptr;

  // Remote-mode page.
  QWidget *m_remotePage = nullptr;
  QLineEdit *m_remoteUrlEdit = nullptr;
  QLineEdit *m_remotePatEdit = nullptr;
  LocationInputWithBrowseButton *m_remoteDestInput = nullptr;

  // Bottom progress (hidden by default; shown during a remote clone).
  QProgressBar *m_progressBar = nullptr;

  // Result of a successful Local-mode open OR remote-mode clone.
  QString m_openedNotebookId;

  // openurl-followups Item 2: cached pointer to the Cancel button so the
  // rejectedButtonClicked override can flip its enabled state during
  // cancellation. Captured in setupUI from
  // getDialogButtonBox()->button(QDialogButtonBox::Cancel).
  QPushButton *m_cancelButton = nullptr;

  // openurl-followups Item 2: true while OpenNotebookController::cloneAndOpen
  // is in flight (between handleRemoteOpen dispatch and onCloneFinished).
  // The rejectedButtonClicked override branches on this flag to either
  // request controller-level cancellation (true) or run the normal reject()
  // path (false).
  bool m_cloneInProgress = false;
};

} // namespace vnotex

#endif // OPENNOTEBOOKDIALOG2_H
