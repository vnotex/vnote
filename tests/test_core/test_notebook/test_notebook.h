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
        void testVersionControllerServer();

        void testNotebookConfigMgrServer();

        void testNotebookBackendServer();

        void testNotebookServer();

        void testBundleNotebookFactoryNewNotebook();

    private:
        QString getTestFolderPath() const;

        QSharedPointer<QTemporaryDir> m_testDir;

        QSharedPointer<vnotex::NameBasedServer<vnotex::IVersionControllerFactory>> m_vcServer;
        QSharedPointer<vnotex::NameBasedServer<vnotex::INotebookConfigMgrFactory>> m_ncmServer;
        QSharedPointer<vnotex::NameBasedServer<vnotex::INotebookBackendFactory>> m_backendServer;
        QSharedPointer<vnotex::NameBasedServer<vnotex::INotebookFactory>> m_nbServer;
    };
} // ns tests

#endif // TEST_NOTEBOOK_H
