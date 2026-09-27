#ifndef NOTEBOOKSYNCINFOCONTROLLER_H
#define NOTEBOOKSYNCINFOCONTROLLER_H

#include <QObject>
#include <QString>
#include <functional>

#include <core/noncopyable.h>
#include <core/services/syncsettings.h>
#include <vxcore/vxcore_types.h>

namespace vnotex {

class ServiceLocator;

// Orchestrates backend-neutral configuration, credential rotation and explicit
// endpoint retirement. Credentials live only in the current operation's captures.
class NotebookSyncInfoController : public QObject, private Noncopyable {
  Q_OBJECT

public:
  explicit NotebookSyncInfoController(ServiceLocator &p_services, const QString &p_notebookId,
                                      QObject *p_parent = nullptr);
  QString notebookName() const;
  QString remoteUrl() const;
  QString backend() const;
  bool syncEnabled() const;
  bool isRawNotebook() const;
  QString lastSyncTime() const;
  void loadInitialData();

  void applyChanges(const SyncSettings &p_settings);
  // Existing notebooks survive every failed enable; only the new-notebook
  // controller owns destructive creation rollback.
  void bootstrapApply(const SyncSettings &p_settings);
  void disableSync();

public slots:
  void confirmUrlChange(bool p_confirmed);

signals:
  void dataLoaded(const QString &p_notebookName, const QString &p_remoteUrl,
                  const QString &p_lastSyncTime);
  void applyComplete(bool p_success);
  void disableComplete(const QString &p_notebookId);
  void error(const QString &p_message);
  void confirmUrlChangeRequested(const QString &p_oldUrl, const QString &p_newUrl);

private:
  void startApply(const SyncSettings &p_settings, bool p_bootstrap);
  void withCredentials(SyncSettings p_settings, std::function<void(const SyncSettings &)> p_next);
  void enable(const SyncSettings &p_settings, bool p_reconfigured);
  void retireAndEnable(const SyncSettings &p_settings);
  void finishFailure(const QString &p_message);

  ServiceLocator &m_services;
  QString m_notebookId;
  bool m_applying = false;
  // A single transient confirmation continuation; reset on cancellation/consumption.
  std::function<void()> m_pendingEndpointChange;
};

} // namespace vnotex

#endif // NOTEBOOKSYNCINFOCONTROLLER_H
