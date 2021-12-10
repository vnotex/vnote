#ifndef TESTS_TASK_TEST_TASK_H
#define TESTS_TASK_TEST_TASK_H

#include <QtTest>
#include <QSharedPointer>

namespace vnotex
{
    class Task;
}

namespace tests
{
    class TestTask : public QObject
    {
        Q_OBJECT
    public:
        explicit TestTask(QObject *p_parent = nullptr);

    private slots:
        void initTestCase();

        // Define test cases here per slot.
        void TestTaskVariableMgr();

    private:
        QSharedPointer<vnotex::Task> createTask() const;
    };
} // ns tests

#endif // TESTS_UTILS_TEST_UTILS_H
