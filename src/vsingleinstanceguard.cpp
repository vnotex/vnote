#include "vsingleinstanceguard.h"
#include <QDebug>

#include "utils/vutils.h"

const QString VSingleInstanceGuard::c_memKey = "vnote_shared_memory";
const int VSingleInstanceGuard::c_magic = 133191933;

VSingleInstanceGuard::VSingleInstanceGuard()
    : m_sharedMemory(c_memKey)
{
}

bool VSingleInstanceGuard::tryRun()
{
    // If we can attach to the sharedmemory, there is another instance running.
    // In Linux, crashes may cause the shared memory segment remains. In this case,
    // this will attach to the old segment, then exit, freeing the old segment.
    if (m_sharedMemory.attach()) {
        qDebug() << "another instance is running";
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
        return true;
    } else {
        qDebug() << "fail to create shared memory segment";
        return false;
    }
}

void VSingleInstanceGuard::openExternalFiles(const QStringList &p_files)
{
    if (p_files.isEmpty()) {
        return;
    }

    if (!m_sharedMemory.isAttached()) {
        if (!m_sharedMemory.attach()) {
            qDebug() << "fail to attach to the shared memory segment"
                     << (m_sharedMemory.error() ? m_sharedMemory.errorString() : "");
            return;
        }
    }

    qDebug() << "try to request another instance to open files" << p_files;

    int idx = 0;
    int tryCount = 100;
    while (tryCount--) {
        qDebug() << "set shared memory one round" << idx << "of" << p_files.size();
        m_sharedMemory.lock();
        SharedStruct *str = (SharedStruct *)m_sharedMemory.data();
        V_ASSERT(str->m_magic == c_magic);
        for (; idx < p_files.size(); ++idx) {
            if (p_files[idx].size() + 1 > FilesBufCount) {
                qDebug() << "skip file since its long name" << p_files[idx];
                // Skip this long long name file.
                continue;
            }

            if (!appendFileToBuffer(str, p_files[idx])) {
                break;
            }
        }

        m_sharedMemory.unlock();

        if (idx < p_files.size()) {
            VUtils::sleepWait(500);
        } else {
            break;
        }
    }
}

bool VSingleInstanceGuard::appendFileToBuffer(SharedStruct *p_str, const QString &p_file)
{
    if (p_file.isEmpty()) {
        return true;
    }

    int strSize = p_file.size();
    if (strSize + 1 > FilesBufCount - p_str->m_filesBufIdx) {
        qDebug() << "no enough space for" << p_file;
        return false;
    }

    // Put the size first.
    p_str->m_filesBuf[p_str->m_filesBufIdx++] = (ushort)strSize;
    const QChar *data = p_file.constData();
    for (int i = 0; i < strSize; ++i) {
        p_str->m_filesBuf[p_str->m_filesBufIdx++] = data[i].unicode();
    }

    qDebug() << "after appended one file" << p_str->m_filesBufIdx << p_file;
    return true;
}

QStringList VSingleInstanceGuard::fetchFilesToOpen()
{
    QStringList files;
    Q_ASSERT(m_sharedMemory.isAttached());
    m_sharedMemory.lock();
    SharedStruct *str = (SharedStruct *)m_sharedMemory.data();
    Q_ASSERT(str->m_magic == c_magic);
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

void VSingleInstanceGuard::showInstance()
{
    if (!m_sharedMemory.isAttached()) {
        if (!m_sharedMemory.attach()) {
            qDebug() << "fail to attach to the shared memory segment"
                     << (m_sharedMemory.error() ? m_sharedMemory.errorString() : "");
            return;
        }
    }

    m_sharedMemory.lock();
    SharedStruct *str = (SharedStruct *)m_sharedMemory.data();
    V_ASSERT(str->m_magic == c_magic);
    str->m_askedToShow = true;
    m_sharedMemory.unlock();

    qDebug() << "try to request another instance to show up";
}

bool VSingleInstanceGuard::fetchAskedToShow()
{
    bool ret = false;

    Q_ASSERT(m_sharedMemory.isAttached());
    m_sharedMemory.lock();
    SharedStruct *str = (SharedStruct *)m_sharedMemory.data();
    Q_ASSERT(str->m_magic == c_magic);
    ret = str->m_askedToShow;
    str->m_askedToShow = false;
    m_sharedMemory.unlock();

    return ret;
}
