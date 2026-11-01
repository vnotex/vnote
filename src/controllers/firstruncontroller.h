#ifndef FIRSTRUNCONTROLLER_H
#define FIRSTRUNCONTROLLER_H

#include <QObject>
#include <QString>

namespace vnotex {

class ServiceLocator;

// Headless controller for the first-run experience (FRE).
// On MainWindowAfterStart, opens the default notebook under Documents, reusing
// an existing notebook or copying the bundled "My Notebook" into an absent or
// empty destination. Runs ONLY when the application version changed AND there
// are zero notebooks. Contains no UI logic; View/MainWindow2 surfaces the
// created or reopened notebook.
class FirstRunController : public QObject {
  Q_OBJECT

public:
  explicit FirstRunController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  ~FirstRunController() override;

  // Whether a default notebook should be created or reopened. True only when the
  // version changed AND there are currently zero notebooks.
  bool shouldCreateDefaultNotebook(bool p_versionChanged) const;

  // Open the default notebook under the resolved parent directory. Copy bundled
  // content only into an absent or empty destination; reuse a non-empty one.
  // Returns true and emits defaultNotebookCreated for a new or reopened notebook.
  // Returns false without a signal for non-directory targets or copy/open failure.
  // Never fall back to copying over an existing notebook when opening fails.
  // On success, pins all-in-one.md when Quick Access is empty and the session
  // can be read (or is absent), before emitting defaultNotebookCreated.
  bool createDefaultNotebook();

  // Override the default notebook parent directory. For testing only,
  // so scenarios run against isolated temp dirs instead of the real Documents.
  void setParentDirOverrideForTesting(const QString &p_dir);

  // Override the bundled-notebook source directory used when materializing the
  // default notebook. For testing only, so scenarios copy from an on-disk
  // fixture (writable) instead of mounting the production vnote_extra.rcc,
  // which is not built in the unit-test target. When set, the rcc is NOT
  // registered.
  void setSourceDirOverrideForTesting(const QString &p_dir);

signals:
  void defaultNotebookCreated(const QString &p_notebookId);

private:
  void onMainWindowAfterStart();

  void initializeQuickAccess(const QString &p_notebookId);

  // Recursively clear the read-only bit on every file under p_dirPath, adding
  // owner/user write permission. Files copied out of a Qt .rcc inherit the
  // resource's read-only permissions; without this the notebook's config.json
  // and notes would be non-writable on disk (vxcore could not update metadata
  // on open, and the user could not edit the bundled notes).
  static void clearReadOnlyRecursively(const QString &p_dirPath);

  ServiceLocator &m_services;

  int m_hookId = -1;

  QString m_parentDirOverride;

  QString m_sourceDirOverride;
};

} // namespace vnotex

#endif // FIRSTRUNCONTROLLER_H
