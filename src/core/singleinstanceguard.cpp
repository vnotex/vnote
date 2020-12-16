#include "singleinstanceguard.h"
#include <QDebug>

#include <utils/utils.h>

using namespace vnotex;

const QString SingleInstanceGuard::c_memKey = "vnotex_shared_memory";
const int SingleInstanceGuard::c_magic = 376686683;

SingleInstanceGuard::SingleInstanceGuard()
    : m_online(false),
      m_sharedMemory(c_memKey)
{
}

bool SingleInstanceGuard::tryRun()
{
    m_online = false;

    // If we can attach to the sharedmemory, there is another instance running.
    // In Linux, crashes may cause the shared memory segment remains. In this case,
    // this will attach to the old segment, then exit, freeing the old segment.
    if (m_sharedMemory.attach()) {
        qInfo() << "another instance is running";
        // so try to show it?
        showInstance();
        return false;
    }

    // Try to create it.
    bool ret = m_sharedMemory.create(sizeof(SharedStruct));
    if (ret) {
        // We created it.
        m_sharedMemory.lock();
        SharedStruct *str = (SharedStruct *)m_sharedMemory.data();
        str->m_magic = c_magic;
        str->m_filesBufIdx = 0;
        str->m_askedToShow = false;
        m_sharedMemory.unlock();

        m_online = true;
        return true;
    } else {
        qCritical() << "fail to create shared memory segment";
        return false;
    }
}

void SingleInstanceGuard::openExternalFiles(const QStringList &p_files)
{
    if (p_files.isEmpty()) {
        return;
    }

    if (!m_sharedMemory.isAttached()) {
        if (!m_sharedMemory.attach()) {
            qCritical() << "fail to attach to the shared memory segment"
                        << (m_sharedMemory.error() ? m_sharedMemory.errorString() : "");
            return;
        }
    }

    int idx = 0;
    int tryCount = 100;
    while (tryCount--) {
        m_sharedMemory.lock();
        SharedStruct *str = (SharedStruct *)m_sharedMemory.data();
        V_ASSERT(str->m_magic == c_magic);
        for (; idx < p_files.size(); ++idx) {
            if (p_files[idx].size() + 1 > FilesBufCount) {
                // Skip this long long name file.
                continue;
            }

            if (!appendFileToBuffer(str, p_files[idx])) {
                break;
            }
        }

        m_sharedMemory.unlock();

        if (idx < p_files.size()) {
            Utils::sleepWait(500);
        } else {
            break;
        }
    }
}

bool SingleInstanceGuard::appendFileToBuffer(SharedStruct *p_str, const QString &p_file)
{
    if (p_file.isEmpty()) {
        return true;
    }

    int strSize = p_file.size();
    if (strSize + 1 > FilesBufCount - p_str->m_filesBufIdx) {
        return false;
    }

    // Put the size first.
    p_str->m_filesBuf[p_str->m_filesBufIdx++] = (ushort)strSize;
    const QChar *data = p_file.constData();
    for (int i = 0; i < strSize; ++i) {
        p_str->m_filesBuf[p_str->m_filesBufIdx++] = data[i].unicode();
    }

    return true;
}

QStringList SingleInstanceGuard::fetchFilesToOpen()
{
    QStringList files;

    if (!m_online) {
        return files;
    }

    Q_ASSERT(m_sharedMemory.isAttached());
    m_sharedMemory.lock();
    SharedStruct *str = (SharedStruct *)m_sharedMemory.data();
    V_ASSERT(str->m_magic == c_magic);
    Q_ASSERT(str->m_filesBufIdx <= FilesBufCount);
    int idx = 0;
    while (idx < str->m_filesBufIdx) {
        int strSize = str->m_filesBuf[idx++];
        Q_ASSERT(strSize <= str->m_filesBufIdx - idx);
        QString file;
        for (int i = 0; i < strSize; ++i) {
            file.append(QChar(str->m_filesBuf[idx++]));
        }

        files.append(file);
    }

    str->m_filesBufIdx = 0;
    m_sharedMemory.unlock();

    return files;
}

void SingleInstanceGuard::showInstance()
{
    if (!m_sharedMemory.isAttached()) {
        if (!m_sharedMemory.attach()) {
            qCritical() << "fail to attach to the shared memory segment"
                        << (m_sharedMemory.error() ? m_sharedMemory.errorString() : "");
            return;
        }
    }

    m_sharedMemory.lock();
    SharedStruct *str = (SharedStruct *)m_sharedMemory.data();
    V_ASSERT(str->m_magic == c_magic);
    str->m_askedToShow = true;
    m_sharedMemory.unlock();
}

bool SingleInstanceGuard::fetchAskedToShow()
{
    if (!m_online) {
        return false;
    }

    Q_ASSERT(m_sharedMemory.isAttached());
    m_sharedMemory.lock();
    SharedStruct *str = (SharedStruct *)m_sharedMemory.data();
    V_ASSERT(str->m_magic == c_magic);
    bool ret = str->m_askedToShow;
    str->m_askedToShow = false;
    m_sharedMemory.unlock();

    return ret;
}

void SingleInstanceGuard::exit()
{
    if (!m_online) {
        return;
    }

    Q_ASSERT(m_sharedMemory.isAttached());
    m_sharedMemory.detach();
    m_online = false;
}
