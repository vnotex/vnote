#include "asyncworker.h"

using namespace vnotex;

AsyncWorker::AsyncWorker(QObject *p_parent)
    : QThread(p_parent)
{
}

void AsyncWorker::stop()
{
    m_askedToStop.fetchAndStoreAcquire(1);
}

bool AsyncWorker::isAskedToStop() const
{
    return m_askedToStop.loadAcquire() == 1;
}


AsyncWorkerWithFunctor::AsyncWorkerWithFunctor(QObject *p_parent)
    : QThread(p_parent)
{
}

void AsyncWorkerWithFunctor::doWork(const Functor &p_functor)
{
    Q_ASSERT(!isRunning());
    m_functor = p_functor;
    start();
}

void AsyncWorkerWithFunctor::run()
{
    Q_ASSERT(m_functor);
    m_functor();
}
