#ifndef ACTIVITYSERVICE_H
#define ACTIVITYSERVICE_H

#include <QElapsedTimer>
#include <QObject>

#include "core/noncopyable.h"

#include <vxcore/vxcore_types.h>

class QTimer;

namespace vnotex {

class HookManager;

// Qt-side activity tracker.
//
// Computes app-focus duration on the Qt side and pushes it into vxcore's
// app-global activity.db via the vxcore_activity_* C API. Also forwards
// "note read" events (FileAfterOpen hook) into vxcore. Note created/edited are
// captured natively inside vxcore via its EventManager subscription, so this
// service does NOT re-count them.
//
// Focus batching: while the app is focused, a periodic timer (~60s) plus
// focus-lost / close flushes accumulated focused milliseconds, so a crash loses
// at most one flush interval of active time.
//
// Main-thread only. All vxcore_activity_* calls are cheap single-row UPSERTs.
class ActivityService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // p_context: vxcore handle (must outlive this service).
  // p_hookManager: for FileAfterOpen / MainWindow lifecycle subscriptions.
  ActivityService(VxCoreContextHandle p_context, HookManager *p_hookManager,
                  QObject *p_parent = nullptr);
  ~ActivityService() override;

  // Flush any pending focused-time delta into vxcore immediately. Safe to call
  // from aboutToQuit. Idempotent.
  void flush();

private:
  // Called when the application activation state changes.
  void onApplicationStateChanged(Qt::ApplicationState p_state);

  // Push the elapsed focused-ms since the last flush into vxcore and reset the
  // baseline. No-op when not currently focused.
  void flushFocusDelta();

  void beginFocus();
  void endFocus();

  VxCoreContextHandle m_context = nullptr;
  HookManager *m_hookManager = nullptr;

  // Periodic flush while focused (~60s cadence).
  QTimer *m_flushTimer = nullptr;

  // Monotonic clock measuring focused wall-time since the last flush.
  QElapsedTimer m_elapsed;
  bool m_focused = false;

  int m_fileAfterOpenHookId = -1;
  int m_mainWindowBeforeCloseHookId = -1;
};

} // namespace vnotex

#endif // ACTIVITYSERVICE_H
