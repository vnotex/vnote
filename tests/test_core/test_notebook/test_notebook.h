#ifndef TEST_NOTEBOOK_H
#define TEST_NOTEBOOK_H

#include <QtTest>
#include <QSharedPointer>

#include <namebasedserver.h>

class QTemporaryDir;

namespace vnotex
{
    class IVersionControllerFactory;
    class INotebookConfigMgrFactory;
    class INotebookBackendFactory;
    class INotebookFactory;
}

namespace tests
{
    class TestNotebook : public QObject
    {
        Q_OBJECT
    public:
        explicit TestNotebook(QObject *p_parent = nullptr);

    private slots:
        // Define test cases here per slot.
        void testNotebookDatabase();
    };
} // ns tests

#endif // TEST_NOTEBOOK_H
