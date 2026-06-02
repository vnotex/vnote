#ifndef EVENTBRIDGE_H
#define EVENTBRIDGE_H

#include <QObject>
#include <QString>
#include <QStringList>

#include <core/noncopyable.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

namespace vnotex {

class EventBridge : public QObject, private Noncopyable {
  Q_OBJECT

public:
  explicit EventBridge(VxCoreContextHandle p_context, QObject *p_parent = nullptr);
  ~EventBridge() override;

  // Idempotently unregister vxcore event subscriptions. Call from aboutToQuit
  // BEFORE SyncService / vxcore_context_destroy teardown to avoid an access
  // violation in ~EventBridge -> vxcore_off_event (mutex on already-freed
  // EventManager). Safe to call multiple times; the dtor also invokes it.
  void shutdown();

signals:
  void syncStarted(const QString &p_notebookId);
  void syncFinished(const QString &p_notebookId, VxCoreError p_result);
  void syncConflict(const QString &p_notebookId);
  void syncConflictFiles(const QString &p_notebookId, const QStringList &p_files);
  void syncShouldRun(const QString &p_notebookId);
  void fileChanged(const QString &p_eventName, const QString &p_notebookId);

private:
  static void onVxCoreEvent(const char *event_name, const char *json_data, void *userdata);

  VxCoreContextHandle m_context;
  bool m_shutDown = false;
};

} // namespace vnotex

#endif // EVENTBRIDGE_H
