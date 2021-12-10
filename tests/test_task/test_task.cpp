#include "test_task.h"

#include <QDebug>
#include <QProcessEnvironment>

#include <task/taskvariablemgr.h>
#include <task/task.h>
#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <core/sessionconfig.h>

using namespace tests;

using namespace vnotex;

TestTask::TestTask(QObject *p_parent)
    : QObject(p_parent)
{
}

void TestTask::initTestCase()
{
    ConfigMgr::initForUnitTest();
}

void TestTask::TestTaskVariableMgr()
{
    TaskVariableMgr mgr(nullptr);
    mgr.init();

    mgr.overrideVariable("notebookFolder", [](const Task *, const QString &val) {
        Q_ASSERT(val.isEmpty());
        return "/home/vnotex/vnote";
    });

    mgr.overrideVariable("notebookFolderName", [](const Task *, const QString &val) {
        Q_ASSERT(val.isEmpty());
        return "vnote";
    });

    mgr.overrideVariable("magic", [](const Task *, const QString &val) {
        if (val.isEmpty()) {
            return QString();
        } else {
            return val;
        }
    });

    auto task = createTask();

    auto result = mgr.evaluate(task.data(), "start ${notebookFolder} end");
    QCOMPARE(result, "start /home/vnotex/vnote end");

    result = mgr.evaluate(task.data(), "start ${notebookFolder} mid ${notebookFolderName} end");
    QCOMPARE(result, "start /home/vnotex/vnote mid vnote end");

    result = mgr.evaluate(task.data(), "${magic:yyyy} ${magic:MM} ${magic:dd}");
    QCOMPARE("yyyy MM dd", result);

    {
        const auto env = QProcessEnvironment::systemEnvironment();
        result = mgr.evaluate(task.data(), "${env:PATH} ${env:QT_PATH} ${env:nonexist}");
        QCOMPARE(result, QString("%1 %2 %3").arg(env.value("PATH"), env.value("QT_PATH"), env.value("nonexist")));
    }

    result = mgr.evaluate(task.data(), "${config:main.core.toolbar_icon_size} ${config:main.core.nonexists} ${config:session.core.system_title_bar}");
    QCOMPARE(result, QString("%1  %2").arg(ConfigMgr::getInst().getCoreConfig().getToolBarIconSize())
                                      .arg(ConfigMgr::getInst().getSessionConfig().getSystemTitleBarEnabled()));
}

QSharedPointer<vnotex::Task> TestTask::createTask() const
{
    return QSharedPointer<Task>(new Task("en_US", "dummy_file", nullptr, nullptr));
}

QTEST_MAIN(tests::TestTask)

