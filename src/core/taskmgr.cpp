#include "taskmgr.h"

#include <QDir>
#include <QDebug>

#include "configmgr.h"
#include "coreconfig.h"
#include "utils/pathutils.h"

using namespace vnotex;

QStringList TaskMgr::s_searchPaths;

TaskMgr::TaskMgr(QObject *parent) 
    : QObject(parent)
{
    
}

const QVector<TaskMgr::TaskInfo> &TaskMgr::getAllTasks() const
{
    return m_tasks;
}

void TaskMgr::addSearchPath(const QString &p_path)
{
    s_searchPaths << p_path;
}

void TaskMgr::loadAvailableTasks()
{
    m_tasks.clear();
    
    for (const auto &pa : s_searchPaths) {
        loadTasks(pa);
    }
}

void TaskMgr::loadTasks(const QString &p_path)
{
    qDebug() << "search for tasks in " << p_path;
    QDir dir(p_path);
    dir.setFilter(QDir::NoDotAndDotDot);
    auto taskEntrys = dir.entryList();
    const auto localeStr = ConfigMgr::getInst().getCoreConfig().getLocaleToUse();
    for (auto &entry : taskEntrys) {
        checkAndAddTaskFile(PathUtils::concatenateFilePath(p_path, entry), localeStr);
    }
}

void TaskMgr::checkAndAddTaskFile(const QString &p_file, const QString &p_locale)
{
    qDebug() << "Check Task Entry: " << p_file << ", locale: " << p_locale;
    
}
