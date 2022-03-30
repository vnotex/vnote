#ifndef ASYNCWORKER_H
#define ASYNCWORKER_H

#include <QThread>

#include <QAtomicInt>

namespace vnotex
{
    class AsyncWorker : public QThread
    {
        Q_OBJECT
    public:
        explicit AsyncWorker(QObject *p_parent = nullptr);

    public slots:
        void stop();

    protected:
        bool isAskedToStop() const;

    private:
        QAtomicInt m_askedToStop = 0;
    };


    class AsyncWorkerWithFunctor : public QThread
    {
        Q_OBJECT
    public:
        typedef std::function<void()> Functor;

        explicit AsyncWorkerWithFunctor(QObject *p_parent = nullptr);

        void doWork(const Functor &p_functor);

    protected:
        void run() Q_DECL_OVERRIDE;

    private:
        Functor m_functor;
    };
}

#endif // ASYNCWORKER_H
