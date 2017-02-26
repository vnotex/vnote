#include "vsingleinstanceguard.h"
#include <QDebug>

const QString VSingleInstanceGuard::m_memKey = "vnote_shared_memory";
const QString VSingleInstanceGuard::m_semKey = "vnote_semaphore";
const int VSingleInstanceGuard::m_magic = 133191933;

VSingleInstanceGuard::VSingleInstanceGuard()
    : m_sharedMemory(m_memKey), m_sem(m_semKey, 1)
{
}

VSingleInstanceGuard::~VSingleInstanceGuard()
{
    if (m_sharedMemory.isAttached()) {
        detachMemory();
    }
}

void VSingleInstanceGuard::detachMemory()
{
    m_sem.acquire();
    m_sharedMemory.detach();
    m_sem.release();
}

bool VSingleInstanceGuard::tryAttach()
{
    m_sem.acquire();
    bool ret = m_sharedMemory.attach();
    m_sem.release();
    if (ret) {
        m_sharedMemory.lock();
        SharedStruct *str = (SharedStruct *)m_sharedMemory.data();
        str->m_activeRequest = 1;
        m_sharedMemory.unlock();
        detachMemory();
    }
    return ret;
}

bool VSingleInstanceGuard::tryRun()
{
    // If we can attach to the sharedmemory, there is another instance running.
    if (tryAttach()) {
        qDebug() << "another instance is running";
        return false;
    }

    // Try to create it
    m_sem.acquire();
    bool ret = m_sharedMemory.create(sizeof(SharedStruct));
    m_sem.release();
    if (ret) {
        // We created it
        m_sharedMemory.lock();
        SharedStruct *str = (SharedStruct *)m_sharedMemory.data();
        str->m_magic = m_magic;
        str->m_activeRequest = 0;
        m_sharedMemory.unlock();
        return true;
    } else {
        // Maybe another thread create it
        if (tryAttach()) {
            qDebug() << "another instance is running";
            return false;
        } else {
            // Something wrong here
            qWarning() << "fail to create or attach shared memory segment";
            return false;
        }
    }
}
