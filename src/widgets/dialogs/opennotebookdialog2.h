#ifndef OPENNOTEBOOKDIALOG2_H
#define OPENNOTEBOOKDIALOG2_H

#include "scrolldialog.h"

#include <QString>

class QButtonGroup;
class QLabel;
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
//   * Local Folder: pick a root folder on disk via QFileDialog::getExistingDirectory
//     and open it through OpenNotebookController::openNotebook().
//   * Remote URL  : provide an HTTPS or file:// URL, an optional PAT (empty
//     means read-only), and a destination parent folder. The dialog suggests
//     the leaf folder name from the URL. Clone wiring (openurl-followups
//     Item 2) drives OpenNotebookController::cloneAndOpen and exposes
//     mid-clone Cancel via OpenNotebookController::cancelClone.
//
// Pure UI: all business logic is delegated to OpenNotebookController. The
// dialog never calls vxcore directly and never touches the credentials store.
//
// On a successful Local-mode open, the dialog emits notebookOpened(QString)
// with the new notebook ID and accept()s. Remote mode wires the controller
// signals (cloneFinished + cloneProgressUpdated) and emits notebookOpened +
// accept() on a successful clone result.
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
  void notebookOpened(const QString &p_notebookId);

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
  void onBrowseRemoteDestClicked();

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

  // Extracts a leaf repository name from the URL. Strips trailing slashes and
  // ".git", then returns the final path segment. Used to auto-suggest the
  // final destination folder from a user-picked parent.
  // Examples:
  //   https://github.com/user/repo.git    -> "repo"
  //   https://github.com/user/repo        -> "repo"
  //   file:///C:/path/to/repo.git/        -> "repo"
  static QString extractRepoNameFromUrl(const QString &p_url);

  // Recompute and write the suggested destination into m_remoteDestEdit based
  // on the currently selected parent directory and the URL field.
  void updateSuggestedDestination();

  // Recompute the Open button enabled state from the active mode's validation.
  void updateOpenButtonState();

  // STUB for remote-mode validation. T22 will move this to
  // OpenNotebookController::validateCloneInput(). Returns valid=true when:
  //   - URL is non-empty AND matches https:// or file:/// scheme
  //   - Destination is non-empty AND does NOT yet exist on disk
  // On failure, populates message with a user-facing reason.
  struct RemoteValidation {
    bool valid = false;
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
  QLineEdit *m_remoteDestEdit = nullptr;
  QPushButton *m_remoteDestBrowseButton = nullptr;
  QLabel *m_remoteDestHintLabel = nullptr;
  QLabel *m_remotePatHintLabel = nullptr;
  // Parent folder the user picked via Browse; the final dest is derived as
  // <parent>/<repoNameFromUrl> and written into m_remoteDestEdit.
  QString m_remoteSelectedParentDir;

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
