#ifndef VSINGLEINSTANCEGUARD_H
#define VSINGLEINSTANCEGUARD_H

#include <QString>
#include <QSharedMemory>
#include <QSystemSemaphore>

class VSingleInstanceGuard
{
public:
    VSingleInstanceGuard();
    ~VSingleInstanceGuard();
    bool tryRun();

private:
    void detachMemory();
    bool tryAttach();

    struct SharedStruct {
        // A magic number to identify if this struct is initialized
        int m_magic;
        // If it is 1, then another instance ask this instance to show itself
        int m_activeRequest;
    };

    static const QString m_memKey;
    static const QString m_semKey;
    static const int m_magic;
    QSharedMemory m_sharedMemory;
    QSystemSemaphore m_sem;
};

#endif // VSINGLEINSTANCEGUARD_H
