#ifndef FIRSTRUNCONTROLLER_H
#define FIRSTRUNCONTROLLER_H

#include <QObject>
#include <QString>

namespace vnotex {

class ServiceLocator;

// Headless controller for the first-run experience (FRE).
// On the MainWindowAfterStart hook, materializes a default notebook
// ("My Notebook") under the Documents folder by copying the bundled notebook
// shipped in Qt resources, then opens it -- but ONLY when the application
// version changed AND there are zero notebooks. Contains no UI logic; the
// View/MainWindow2 is responsible for surfacing the created notebook.
class FirstRunController : public QObject {
  Q_OBJECT

public:
  explicit FirstRunController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  ~FirstRunController() override;

  // Whether a default notebook should be created. Returns true only when the
  // version changed AND there are currently zero notebooks.
  bool shouldCreateDefaultNotebook(bool p_versionChanged) const;

  // Create the default notebook under the resolved parent dir by copying the
  // bundled notebook from Qt resources, then opening it. Returns true on
  // success (and emits defaultNotebookCreated). Skips (returns false, no
  // signal) when the target "my_notebook" path is an existing file or a
  // non-empty directory, or when the copy/open fails.
  bool createDefaultNotebook();

  // Override the parent directory used by resolveParentDir(). For testing only,
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

  QString resolveParentDir() const;

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
