#ifndef NEWNOTEBOOKCONTROLLER_H
#define NEWNOTEBOOKCONTROLLER_H

#include <QObject>
#include <QString>

#include <core/services/notebookcoreservice.h>
#include <core/services/syncsettings.h>

class QProgressDialog;
class QWidget;

namespace vnotex {

class ServiceLocator;

// Input data structure for creating a new notebook.
struct NewNotebookInput {
  QString name;
  QString description;
  QString rootFolderPath;
  QString assetsFolder = QStringLiteral("vx_assets");
  NotebookType type = NotebookType::Bundled;
  // Empty inherits the global editor setting; overrides apply only to bundled notebooks.
  QString lineEnding;
  // Creation selection: none, git, or webdav. Enabled settings must match it.
  QString syncMethod = QStringLiteral("none");
  SyncSettings syncSettings;
};

// Result structure for notebook creation.
struct NewNotebookResult {
  bool success = false;
  QString notebookId;
  QString errorMessage;
};

// Validation result structure.
struct ValidationResult {
  bool valid = true;
  QString message;
};

// Controller for new notebook creation operations.
// Handles business logic: validation, duplicate checking, notebook creation.
// View (NewNotebookDialog2) collects input and displays results.
class NewNotebookController : public QObject {
  Q_OBJECT

public:
  explicit NewNotebookController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Validate notebook name.
  ValidationResult validateName(const QString &p_name) const;

  // Validate root folder path.
  // Checks: legal path, empty or non-existent, no duplicate notebook.
  ValidationResult validateRootFolder(const QString &p_path,
                                      NotebookType p_type = NotebookType::Bundled) const;

  // Validate all inputs.
  ValidationResult validateAll(const NewNotebookInput &p_input) const;

  // Build config JSON string from input for vxcore.
  static QString buildConfigJson(const NewNotebookInput &p_input);

  // Create a new notebook with the given input.
  // Returns result with success status and notebook ID or error message.
  NewNotebookResult createNotebook(const NewNotebookInput &p_input);

  // Bootstrap sync on a notebook that already exists (per ADR-7:
  // CREATE-THEN-ENABLE). Caller MUST have invoked createNotebook() first; this
  // method only fires enableSync (with credentials) via SyncService and persists
  // the remote URL into the notebook config on success. On failure the
  // notebook is closed and its on-disk root directory is removed recursively
  // (per ADR-2: cleanup uses closeNotebook + QDir::removeRecursively, since
  // there is no notebook_delete C API).
  //
  // While the operation runs an indeterminate, NON-cancellable QProgressDialog
  // is shown parented at @p_dialogParent. Sync cannot be cancelled once
  // started.
  //
  // Emits exactly one of:
  //   * bootstrapSucceeded(notebookId) on VXCORE_OK,
  //   * bootstrapFailed(notebookId, errorMessage) otherwise.
  //
  // Credentials are forwarded transiently and never cached on this controller.
  void bootstrapSync(const QString &p_notebookId, const SyncSettings &p_settings,
                     QWidget *p_dialogParent);

  // Create the indeterminate progress modal used by bootstrapSync. Exposed as
  // a static helper so the T14 modal QA scenario can construct and inspect the
  // modal without driving the full bootstrap flow. Returned dialog is heap-
  // allocated; caller owns the lifetime (close + deleteLater).
  //
  // Per the plan rule "Sync cannot be cancelled once started" the dialog has
  // NO cancel button (setCancelButton(nullptr)).
  static QProgressDialog *createBootstrapModal(QWidget *p_parent);

signals:
  void bootstrapSucceeded(const QString &p_notebookId);
  void bootstrapFailed(const QString &p_notebookId, const QString &p_errorMessage);

private:
  ServiceLocator &m_services;
};

} // namespace vnotex

#endif // NEWNOTEBOOKCONTROLLER_H
