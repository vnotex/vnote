#ifndef NOTIFICATIONROUTER_H
#define NOTIFICATIONROUTER_H

#include <QObject>
#include <QString>

#include <vxcore/vxcore.h>

namespace vnotex {

class ImageHostAsyncResult;
class ServiceLocator;

// Translates subsystem failure signals into NotificationMessages.
//
// This owns attention/dedup POLICY ONLY -- never recovery logic. NotebookExplorer2
// keeps its sync filtering, state refresh and credential-retry arming; it merely
// stops popping modals and emits a presentation signal instead.
//
// The constructor deliberately takes ONLY ServiceLocator and holds no widget
// pointers, so the header stays widget-free and the unit test can drive the
// widget-owned sources through the public slots without constructing any widget.
// MainWindow2 owns the three connections from its private members into those
// slots, plus the one connection out of openSyncInfoRequested.
//
// WHY A ROUTER AND NOT notify() CALLS IN EACH SERVICE: most of these failures
// ALREADY had signals with zero receivers (BufferService::bufferAutoSaveAborted
// has existed unlistened-to). The failure was never that core services could not
// speak, but that nobody listened. Injecting NotificationService into every
// service would also drag the notification bus into ~80 pure-core test targets.
class NotificationRouter : public QObject {
  Q_OBJECT

public:
  explicit NotificationRouter(ServiceLocator &p_services, QObject *p_parent = nullptr);

  ~NotificationRouter() override;

signals:
  // A notification action asked to open Sync Info for a specific notebook.
  // MainWindow2 forwards this to NotebookExplorer2::openSyncInfo. The router
  // never constructs a dialog.
  void openSyncInfoRequested(const QString &p_notebookId);

public slots:
  // --- Widget-owned sources; connected by MainWindow2. ---

  // From NotebookExplorer2::syncUserMessageRequested.
  void onSyncUserMessageRequested(const QString &p_notebookId, VxCoreError p_code,
                                  const QString &p_title, const QString &p_text,
                                  const QString &p_details);

  // From NotebookExplorer2::syncIncidentRetryRequested. Retires the notebook's
  // sync incident so the NEXT failure interrupts again.
  void onSyncIncidentRetryRequested(const QString &p_notebookId);

  // From ViewArea2::viewWindowCreationFailed.
  void onViewWindowCreationFailed(const QString &p_fileType, const QString &p_path);

  // --- BufferService, connected internally through asQObject(). ---
  //
  // These need to be named slots because BufferService privately inherits its
  // QObject base and only exposes a bare QObject*, so the connection cannot use
  // the pointer-to-member form.
  void onBufferAutoSaveAborted(const QString &p_bufferId);
  void onBufferSaveError(const QString &p_bufferId, const QString &p_errorMsg);
  void onBufferAutoSaved(const QString &p_bufferId);

private:
  void connectServiceSources();

  // Raise one notification per extra-data folder that ConfigMgr2 failed to
  // install. This is a PULL, not a push: ConfigMgr2::initAfterQtAppStarted()
  // runs in main() long before this router exists (and ConfigMgr2 holds no
  // ServiceLocator, so it cannot reach NotificationService anyway), so the
  // failures are read once at MainWindowAfterStart instead.
  void reportExtraDataFailures();

  // Retire every sync incident for a notebook. Called at the boundaries where
  // the incident genuinely ends: sync succeeded, enable succeeded, credentials
  // updated, sync disabled, and manual retry.
  //
  // Deliberately NOT called on notebook switch: an unresolved failure on another
  // notebook is still unresolved, and the user has not acted on it.
  void retireSyncIncidents(const QString &p_notebookId);

  static QString syncDedupKey(VxCoreError p_code, const QString &p_notebookId);

  ServiceLocator &m_services;

  // MainWindowAfterStart subscription used by reportExtraDataFailures().
  int m_afterStartHookId = -1;
};

} // namespace vnotex

#endif // NOTIFICATIONROUTER_H
