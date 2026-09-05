#ifndef RECYCLEBINCONTROLLER_H
#define RECYCLEBINCONTROLLER_H

#include <QFuture>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

#include <vxcore/vxcore_types.h>

namespace vnotex {

class ServiceLocator;
class PreparedRecycleBinCleanup;
struct RecycleBinCleanupResult;

// Result structure for recycle bin operations.
struct RecycleBinResult {
  bool success = false;
  QString errorMessage;
  QString path; // For operations that return a path.
};

// Controller for recycle bin operations on notebooks.
// Handles business logic: validation, confirmation, service calls.
// View (NotebookExplorer2) triggers actions and displays results.
class RecycleBinController : public QObject {
  Q_OBJECT

public:
  explicit RecycleBinController(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~RecycleBinController() override;

  void startAutomaticCleanup();
  void setNowProviderForTesting(std::function<qint64()> p_provider);

  // Check if recycle bin is supported for the given notebook.
  // Returns empty string if not supported, otherwise returns the path.
  QString getRecycleBinPath(const QString &p_notebookId) const;

  // Get the notebook name for display in dialogs.
  QString getNotebookName(const QString &p_notebookId) const;

  // Prepare the recycle bin folder for opening.
  // Creates the folder if it doesn't exist.
  // Returns result with the path to open, or error message.
  RecycleBinResult prepareRecycleBinPath(const QString &p_notebookId);

  // Empty the recycle bin after confirmation.
  // Returns result with success status or error message.
  RecycleBinResult emptyRecycleBin(const QString &p_notebookId);

signals:
  void automaticCleanupFinished(const QString &p_notebookId, VxCoreError p_error,
                                int p_removedCount);

private:
  void queueAutomaticCleanup(const QString &p_notebookId);
  void processNextAutomaticCleanup();
  void finishAutomaticCleanup(const QString &p_notebookId, const RecycleBinCleanupResult &p_result);

  ServiceLocator &m_services;
  std::function<qint64()> m_nowProvider;
  std::shared_ptr<std::atomic_bool> m_stopToken;
  std::shared_ptr<PreparedRecycleBinCleanup> m_activePrepared;
  QFuture<void> m_cleanupFuture;
  QQueue<QString> m_pendingNotebookIds;
  QSet<QString> m_queuedNotebookIds;
  int m_afterStartHookId = -1;
  int m_afterOpenHookId = -1;
  bool m_automaticCleanupStarted = false;
};

} // namespace vnotex

#endif // RECYCLEBINCONTROLLER_H
