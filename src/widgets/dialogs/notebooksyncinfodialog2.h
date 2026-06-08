#ifndef NOTEBOOKSYNCINFODIALOG2_H
#define NOTEBOOKSYNCINFODIALOG2_H

#include <QString>
#include <QStringList>

#include "scrolldialog.h"

#include <vxcore/vxcore_types.h>

class QComboBox; // Reserved for future use by T11 (e.g. backend selector).
class QLabel;
class QLineEdit;
class QPushButton;

namespace vnotex {

class NotebookSyncInfoController;
class ServiceLocator;

// NotebookSyncInfoDialog2 - View for inspecting/editing the git sync settings
// of a single sync-enabled notebook.
//
// Layout (top to bottom):
//   * Notebook         (read-only label)
//   * Remote URL       (editable QLineEdit)
//   * (italic hint reminding that the remote repo must already exist)
//   * Personal Access Token (editable, password-masked QLineEdit; placeholder
//                       prompts user to leave blank to keep the existing PAT)
//   * Last Sync        (read-only label)
//   * Current State    (read-only label, color-coded by SyncService state)
//   * Disable Sync button (destructive; confirms via QMessageBox::warning)
//   * Standard dialog buttons: Ok | Apply | Reset | Cancel
//
// Bootstrap mode (entered via setBootstrapMode(true)) is used by T15 when the
// dialog is auto-opened immediately after a Git-sync notebook is created. In
// bootstrap mode the Disable, Apply and Reset buttons are hidden, and the Ok
// button text becomes "Bootstrap" to communicate that completing the dialog
// performs the initial enable+push.
//
// The dialog OWNS its controller (heap-allocated, parented to the dialog).
// The controller (created in T11) performs all persistence: reading the
// notebook config, talking to SyncService, and storing/clearing PATs via
// SyncCredentialsStore. The dialog itself NEVER calls vxcore directly and
// NEVER reads/writes the credentials store directly.
//
// The PAT field is NEVER prefilled with the actual stored token. Users must
// re-enter a PAT (or leave the field blank to keep the existing one).
class NotebookSyncInfoDialog2 : public ScrollDialog {
  Q_OBJECT

public:
  explicit NotebookSyncInfoDialog2(ServiceLocator &p_services, const QString &p_notebookId,
                                   QWidget *p_parent = nullptr);

  // Pre-create overload: used by NewNotebookDialog2 to collect URL + PAT
  // before the notebook exists in vxcore. The controller is NOT constructed
  // in this mode; values are read back via enteredRemoteUrl()/enteredPat().
  explicit NotebookSyncInfoDialog2(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  // Set the notebook name to display in the pre-create dialog header.
  // Only meaningful when isPreCreateMode() is true. If p_name is empty,
  // the notebook name label is hidden.
  void setPreCreateNotebookName(const QString &p_name);

  // Toggle bootstrap mode. In bootstrap mode the dialog represents an initial
  // setup (post-create), the Disable/Apply/Reset buttons are hidden, and the
  // Ok button is relabeled to "Bootstrap". Also sets a dynamic Qt property
  // ("bootstrapMode") so tests can discover the mode without relying on text.
  void setBootstrapMode(bool p_enabled);

  // Pre-create mode accessors. Return values entered by the user without
  // persisting to vxcore. Only meaningful when isPreCreateMode() is true.
  QString enteredRemoteUrl() const;
  QString enteredPat() const;
  bool isPreCreateMode() const;

  // True if the user has typed changes that have not yet been applied. URL is
  // dirty when it differs from the last-applied value; PAT is dirty whenever
  // the field is non-empty (any text means the user wants to replace the PAT).
  bool changesPending() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

  void appliedButtonClicked() Q_DECL_OVERRIDE;

  void resetButtonClicked() Q_DECL_OVERRIDE;

private slots:
  // Dirty-flag updater wired to PAT/URL textChanged.
  void onFieldEdited();

  // Disable Sync button click handler: shows a destructive-action confirmation
  // QMessageBox and, on Ok, asks the controller to disable sync.
  void onDisableSyncClicked();

  // SyncService signal handlers. All filter by m_notebookId so unrelated
  // notebook events do not affect this dialog.
  void onSyncStarted(const QString &p_notebookId);
  void onSyncFinished(const QString &p_notebookId, VxCoreError p_result);
  void onSyncFailed(const QString &p_notebookId, VxCoreError p_code, const QString &p_message);
  void onConflictsDetected(const QString &p_notebookId, const QStringList &p_conflictFiles);

  // B1: Controller asks the user to confirm a destructive URL change on a
  // sync-registered (S5) notebook. Shows a QMessageBox::warning; on Yes,
  // forwards confirmUrlChange(true) to the controller (which then runs the
  // atomic disable+wipe+re-enable). On No, forwards confirmUrlChange(false)
  // and reverts the URL field text to p_oldUrl. The dialog stays open in
  // either case; closure happens later via applyComplete (success) or stays
  // open on failure (B2 error path).
  void onConfirmUrlChange(const QString &p_oldUrl, const QString &p_newUrl);

  // B2: Controller reports a failure (bootstrap, applyChanges, URL re-
  // change, etc.). Shows a QMessageBox::critical so the user sees the
  // message and can retry; the dialog is NEVER closed by this slot. Empty
  // messages are rendered with a generic fallback rather than suppressed.
  void onError(const QString &p_message);

private:
  enum class SyncStateLevel { Idle, Syncing, Conflict, Error };

  void setupUI();

  // Subscribe to SyncService signals (real services per ADR-6). Both ends are
  // GUI-thread, so default Qt::AutoConnection resolves to DirectConnection.
  void connectSyncServiceSignals();

  // Render the Current State label with the appropriate text/color.
  void setCurrentStateLabel(SyncStateLevel p_level, const QString &p_text);

  // Recompute Apply/Reset enabled state based on changesPending() (no-op while
  // in bootstrap mode where those buttons are hidden).
  void refreshDirtyButtons();

  // T29: Show / hide the read-only banner based on the notebook's current
  // read-only state (queried via NotebookCoreService::isNotebookReadOnly).
  // Idempotent; safe to call multiple times. No-op in pre-create mode (no
  // notebook ID yet to query).
  void refreshReadOnlyBanner();

  ServiceLocator &m_services;

  QString m_notebookId;

  // Controller is heap-allocated and parented to this dialog. Created by T11
  // (the controller class itself is owned by T11 - see notebook-git-sync-ui
  // plan). T10 only declares the slot for it; the pointer remains nullptr
  // here so the dialog VIEW is buildable on its own. Every call site that
  // would invoke m_controller is guarded with a nullptr check.
  NotebookSyncInfoController *m_controller = nullptr;

  // Read-only labels and editable inputs.
  QLabel *m_notebookNameLabel = nullptr;
  // T29: Banner shown at the top of the dialog when the notebook is
  // read-only (no PAT). Explains close-and-reopen flow. Hidden for writable
  // notebooks and in pre-create mode. Lazily wrapped in italic warning
  // style; not themed via QSS to keep the message visually distinct from
  // regular labels.
  QLabel *m_readOnlyBannerLabel = nullptr;
  QLineEdit *m_remoteUrlEdit = nullptr;
  QLabel *m_remoteUrlHintLabel = nullptr;
  QLineEdit *m_patEdit = nullptr;
  QLabel *m_lastSyncLabel = nullptr;
  QLabel *m_currentStateLabel = nullptr;
  QPushButton *m_disableSyncButton = nullptr;

  // Last-applied URL; used by changesPending() to detect dirty edits.
  QString m_lastAppliedRemoteUrl;

  // Bootstrap mode flag; mirrors the dynamic "bootstrapMode" Qt property.
  bool m_bootstrapMode = false;

  // Pre-create mode flag; set to true when constructed via the pre-create
  // overload. In this mode, m_controller is nullptr and acceptedButtonClicked
  // bypasses applyChanges.
  bool m_preCreateMode = false;

  // T29: Cached read-only state, refreshed at dialog open. Drives both the
  // banner visibility and the "close-and-reopen" modal shown after a
  // successful PAT save. Cached (rather than queried per use) so the modal
  // path agrees with the banner the user just saw — querying mid-flow
  // would race a hypothetical mid-dialog RO transition (which is forbidden
  // per Metis Important #9/#10 but we are defensive).
  bool m_isReadOnlyNotebook = false;
};

} // namespace vnotex

#endif // NOTEBOOKSYNCINFODIALOG2_H
