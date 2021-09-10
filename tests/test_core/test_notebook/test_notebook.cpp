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

#include "testnotebookdatabase.h"

using namespace tests;

using namespace vnotex;

TestNotebook::TestNotebook(QObject *p_parent)
    : QObject(p_parent)
{
}

void TestNotebook::testNotebookDatabase()
{
    TestNotebookDatabase test;
    test.test();
}

QTEST_MAIN(tests::TestNotebook)
