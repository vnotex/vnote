#ifndef WORKQUEUEDRAINTHREAD_H
#define WORKQUEUEDRAINTHREAD_H

#include <atomic>

#include <QThread>

#include <core/noncopyable.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

namespace vnotex {

class WorkQueueDrainThread : public QThread, private Noncopyable {
  Q_OBJECT

public:
  explicit WorkQueueDrainThread(VxCoreContextHandle p_context, QObject *p_parent = nullptr);

  void requestStop();

protected:
  void run() override;

private:
  VxCoreContextHandle m_context;
  std::atomic<bool> m_stopRequested{false};
};

} // namespace vnotex

#endif // WORKQUEUEDRAINTHREAD_H
