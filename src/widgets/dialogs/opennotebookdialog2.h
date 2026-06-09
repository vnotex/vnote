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

// OpenNotebookDialog2 - View for opening an existing notebook.
// Two modes:
//   * Local Folder: pick a root folder on disk via QFileDialog::getExistingDirectory
//     and open it through OpenNotebookController::openNotebook().
//   * Remote URL  : provide an HTTPS or file:// URL, an optional PAT (empty
//     means read-only), and a destination parent folder. The dialog suggests
//     the leaf folder name from the URL. Clone wiring lives in T22; T24
//     stubs the Open action with an informational message.
//
// Pure UI: all business logic is delegated to OpenNotebookController. The
// dialog never calls vxcore directly and never touches the credentials store.
//
// On a successful Local-mode open, the dialog emits notebookOpened(QString)
// with the new notebook ID and accept()s. Remote mode is wired in T25 once
// T22 supplies cloneAndOpen()/cloneFinished/cloneProgressUpdated.
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

private slots:
  void onModeChanged();
  void onLocalRootChanged();
  void onRemoteFieldsChanged();
  void onBrowseRemoteDestClicked();

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

  // Remote-mode Open click: T24 STUB. Shows QMessageBox::information stating
  // the remote clone path is wired in T25. No controller call, no signal.
  void handleRemoteOpenStubbed();

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

  // Bottom progress (hidden by default; T22 will show during clone).
  QProgressBar *m_progressBar = nullptr;

  // Result of a successful Local-mode open.
  QString m_openedNotebookId;
};

} // namespace vnotex

#endif // OPENNOTEBOOKDIALOG2_H
