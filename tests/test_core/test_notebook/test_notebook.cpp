#include "test_notebook.h"

#include <QDebug>
#include <QTemporaryDir>
#include <QFileInfo>

#include <versioncontroller/dummyversioncontrollerfactory.h>
#include <versioncontroller/iversioncontroller.h>
#include <notebookconfigmgr/vxnotebookconfigmgrfactory.h>
#include <notebookconfigmgr/inotebookconfigmgr.h>
#include <notebookconfigmgr/bundlenotebookconfigmgr.h>
#include <notebookbackend/localnotebookbackendfactory.h>
#include <notebookbackend/inotebookbackend.h>
#include <notebook/bundlenotebookfactory.h>
#include <notebook/notebook.h>
#include <notebook/notebookparameters.h>
#include <utils/pathutils.h>

using namespace tests;

using namespace vnotex;

TestNotebook::TestNotebook(QObject *p_parent)
    : QObject(p_parent)
{
    m_testDir.reset(new QTemporaryDir);
    Q_ASSERT(m_testDir->isValid());
}

void TestNotebook::testVersionControllerServer()
{
    Q_ASSERT(!m_vcServer);

    m_vcServer.reset(new NameBasedServer<IVersionControllerFactory>);

    // Dummy Version Controller.
    auto dummyFactory = QSharedPointer<DummyVersionControllerFactory>::create();
    m_vcServer->registerItem(dummyFactory->getName(), dummyFactory);

    auto factory = m_vcServer->getItem(dummyFactory->getName());
    auto dummyVC = factory->createVersionController();
    QCOMPARE(dummyVC->getName(), dummyFactory->getName());
}

void TestNotebook::testNotebookConfigMgrServer()
{
    Q_ASSERT(!m_ncmServer);

    m_ncmServer.reset(new NameBasedServer<INotebookConfigMgrFactory>);

    // VX Notebook Config Manager.
    auto vxFactory = QSharedPointer<VXNotebookConfigMgrFactory>::create();
    m_ncmServer->registerItem(vxFactory->getName(), vxFactory);

    auto factory = m_ncmServer->getItem(vxFactory->getName());
    auto vxConfigMgr = factory->createNotebookConfigMgr(nullptr);
    QCOMPARE(vxConfigMgr->getName(), vxFactory->getName());
}

void TestNotebook::testNotebookBackendServer()
{
    Q_ASSERT(!m_backendServer);

    m_backendServer.reset(new NameBasedServer<INotebookBackendFactory>);

    // Local Notebook Backend.
    auto localFactory = QSharedPointer<LocalNotebookBackendFactory>::create();
    m_backendServer->registerItem(localFactory->getName(), localFactory);

    auto factory = m_backendServer->getItem(localFactory->getName());
    auto localBackend = factory->createNotebookBackend("");
    QCOMPARE(localBackend->getName(), localFactory->getName());
}

void TestNotebook::testNotebookServer()
{
    Q_ASSERT(!m_nbServer);

    m_nbServer.reset(new NameBasedServer<INotebookFactory>);

    // Bundle Notebook.
    auto bundleFacotry = QSharedPointer<BundleNotebookFactory>::create();
    m_nbServer->registerItem(bundleFacotry->getName(), bundleFacotry);

    auto factory = m_nbServer->getItem(bundleFacotry->getName());
    QVERIFY(factory == bundleFacotry);
}

void TestNotebook::testBundleNotebookFactoryNewNotebook()
{
    auto nbFactory = m_nbServer->getItem("bundle.vnotex");

    NotebookParameters para;
    para.m_name = "test_notebook";
    para.m_description = "notebook description";
    para.m_rootFolderPath = getTestFolderPath();
    para.m_notebookBackend = m_backendServer->getItem("local.vnotex")
                                            ->createNotebookBackend(para.m_rootFolderPath);
    para.m_versionController = m_vcServer->getItem("dummy.vnotex")->createVersionController();
    para.m_notebookConfigMgr = m_ncmServer->getItem("vx.vnotex")->createNotebookConfigMgr(para.m_notebookBackend);

    auto notebook = nbFactory->newNotebook(para);

    // Verify the notebook is created.
    QVERIFY(QDir(para.m_rootFolderPath).exists());
    auto configMgr = dynamic_cast<BundleNotebookConfigMgr *>(para.m_notebookConfigMgr.data());
    const auto notebookConfigFolder = PathUtils::concatenateFilePath(para.m_rootFolderPath,
                                                                     configMgr->getConfigFolderName());
    const auto notebookConfigPath = PathUtils::concatenateFilePath(notebookConfigFolder,
                                                                   configMgr->getConfigName());
    QVERIFY(QFileInfo::exists(notebookConfigPath));
}

QString TestNotebook::getTestFolderPath() const
{
    return m_testDir->path();
}

QTEST_MAIN(tests::TestNotebook)
