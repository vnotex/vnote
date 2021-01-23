#include "taskmgr.h"

#include <QDir>
#include <QDebug>

#include "configmgr.h"
#include "coreconfig.h"
#include "utils/pathutils.h"
#include "utils/fileutils.h"

using namespace vnotex;

QStringList TaskMgr::s_searchPaths;

TaskMgr::TaskMgr(QObject *parent) 
    : QObject(parent)
{
    loadAvailableTasks();
}

const TaskInfoList &TaskMgr::getAllTasks() const
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
    const auto taskFiles = FileUtils::entryListRecursively(p_path, {"*.json"});
    const auto localeStr = ConfigMgr::getInst().getCoreConfig().getLocaleToUse();
    for (auto &entry : taskFiles) {
        checkAndAddTaskFile(PathUtils::concatenateFilePath(p_path, entry), localeStr);
    }
}

void TaskMgr::checkAndAddTaskFile(const QString &p_file, const QString &p_locale)
{
    if (Task::isValidTaskFile(p_file)) {
        auto ptr = Task::getTaskInfo(p_file, p_locale);
        m_tasks.push_back(ptr);
        qDebug() << "add task" << ptr->m_name << ptr->m_filePath;
    }
}
