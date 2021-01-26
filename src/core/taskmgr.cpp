#include "taskmgr.h"

#include <QDir>
#include <QDebug>

#include "configmgr.h"
#include "coreconfig.h"
#include "utils/pathutils.h"
#include "utils/fileutils.h"
#include "vnotex.h"

using namespace vnotex;

QStringList TaskMgr::s_searchPaths;

TaskMgr::TaskMgr(QObject *parent) 
    : QObject(parent)
{
    loadAvailableTasks();
}

const QVector<Task*> &TaskMgr::getAllTasks() const
{
    return m_tasks;
}

void TaskMgr::refresh()
{
    loadAvailableTasks();
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
    const auto taskFiles = FileUtils::entryListRecursively(p_path, {"*.json"});
    for (auto &entry : taskFiles) {
        checkAndAddTaskFile(PathUtils::concatenateFilePath(p_path, entry));
    }
}

void TaskMgr::checkAndAddTaskFile(const QString &p_file)
{
    if (Task::isValidTaskFile(p_file)) {
        const auto localeStr = ConfigMgr::getInst().getCoreConfig().getLocaleToUse();
        m_tasks.push_back(Task::fromFile(p_file, localeStr, this));
        qDebug() << "add task" << p_file;
    }
}

void TaskMgr::deleteTask(Task *p_task)
{
    FileUtils::removeFile(p_task->getFile());
    for (int i = 0; i < m_tasks.size(); i++) {
        if (m_tasks.at(i) == p_task) {
            m_tasks.remove(i);
            break;
        }
    }
}
