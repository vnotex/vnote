#include "workqueuedrainthread.h"

#include <QDebug>

using namespace vnotex;

WorkQueueDrainThread::WorkQueueDrainThread(VxCoreContextHandle p_context, QObject *p_parent)
    : QThread(p_parent), m_context(p_context) {
  setObjectName(QStringLiteral("VNoteSyncDrain"));
}

void WorkQueueDrainThread::run() {
  qDebug() << "WorkQueueDrainThread: started";
  while (!m_stopRequested.load(std::memory_order_relaxed)) {
    vxcore_work_queue_process_next(m_context, "sync", 500);
  }
  qDebug() << "WorkQueueDrainThread: stopped";
}

void WorkQueueDrainThread::requestStop() {
  m_stopRequested.store(true, std::memory_order_relaxed);
  vxcore_work_queue_shutdown(m_context, "sync");
}
