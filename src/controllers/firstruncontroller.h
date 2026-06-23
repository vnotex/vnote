#ifndef FIRSTRUNCONTROLLER_H
#define FIRSTRUNCONTROLLER_H

#include <QObject>
#include <QString>

namespace vnotex {

class ServiceLocator;

// Headless controller for the first-run experience (FRE).
// On the MainWindowAfterStart hook, creates a default bundled notebook
// ("My Notebook") under the Documents folder, but ONLY when the application
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

  // Create the default bundled notebook under the resolved parent dir. Returns
  // true on success (and emits defaultNotebookCreated). Skips (returns false,
  // no signal) when the target "my_notebook" path is an existing file or a
  // non-empty directory, or when creation fails.
  bool createDefaultNotebook();

  // Override the parent directory used by resolveParentDir(). For testing only,
  // so scenarios run against isolated temp dirs instead of the real Documents.
  void setParentDirOverrideForTesting(const QString &p_dir);

signals:
  void defaultNotebookCreated(const QString &p_notebookId);

private:
  void onMainWindowAfterStart();

  QString resolveParentDir() const;

  ServiceLocator &m_services;

  int m_hookId = -1;

  QString m_parentDirOverride;
};

} // namespace vnotex

#endif // FIRSTRUNCONTROLLER_H
