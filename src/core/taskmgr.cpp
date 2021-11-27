#include "taskmgr.h"

#include <QDir>
#include <QDebug>
#include <QFileSystemWatcher>
#include <QJsonDocument>

#include "configmgr.h"
#include "coreconfig.h"
#include "utils/pathutils.h"
#include "utils/fileutils.h"
#include "vnotex.h"
#include "notebookmgr.h"
#include "notebookconfigmgr/bundlenotebookconfigmgr.h"

using namespace vnotex;

QStringList TaskMgr::s_searchPaths;

TaskMgr::TaskMgr(QObject *parent) 
    : QObject(parent)
{
    m_watcher = new QFileSystemWatcher(this);
}

void TaskMgr::init()
{   
    // load all tasks and watch them
    loadAllTask();
    
    connect(&VNoteX::getInst().getNotebookMgr(), &NotebookMgr::currentNotebookChanged,
            this, &TaskMgr::loadAllTask);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &TaskMgr::loadAllTask);
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &TaskMgr::loadAllTask);
}

void TaskMgr::refresh()
{
    loadAvailableTasks();
}

QVector<Task*> TaskMgr::getAllTasks() const
{
    QVector<Task*> tasks;
    tasks.append(m_appTasks);
    tasks.append(m_userTasks);
    tasks.append(m_notebookTasks);
    return tasks;
}

const QVector<Task *> &TaskMgr::getAppTasks() const
{
    return m_appTasks;
}

const QVector<Task *> &TaskMgr::getUserTasks() const
{
    return m_userTasks;
}

const QVector<Task *> &TaskMgr::getNotebookTasks() const
{
    return m_notebookTasks;
}

void TaskMgr::addSearchPath(const QString &p_path)
{
    s_searchPaths << p_path;
}

QString TaskMgr::getNotebookTaskFolder()
{
    const auto &notebookMgr = VNoteX::getInst().getNotebookMgr();
    auto id = notebookMgr.getCurrentNotebookId();
    if (id == Notebook::InvalidId) return QString();
    auto notebook = notebookMgr.findNotebookById(id);
    if (!notebook) return QString();
    auto configMgr = notebook->getConfigMgr();
    if (!configMgr) return QString();
    auto configMgrName = configMgr->getName();
    if (configMgrName == "vx.vnotex") {
        QDir dir(notebook->getRootFolderAbsolutePath());
        dir.cd(BundleNotebookConfigMgr::getConfigFolderName());
        if (!dir.cd("tasks")) return QString();
        return dir.absolutePath();
    } else {
        qWarning() << "Unknow notebook config type"<< configMgrName <<"task will not be load.";
    }
    return QString();
}

void TaskMgr::addWatchPaths(const QStringList &list)
{
    if (list.isEmpty()) return ;
    qDebug() << "addWatchPaths" << list;
    m_watcher->addPaths(list);
}

void TaskMgr::clearWatchPaths()
{
    auto entrys = m_watcher->files();
    if (!entrys.isEmpty()) {
        m_watcher->removePaths(entrys);
    }
    entrys = m_watcher->directories();
    if (!entrys.isEmpty()) {
        m_watcher->removePaths(entrys);
    }
}

void TaskMgr::clearTasks()
{
    m_appTasks.clear();
    m_userTasks.clear();
    m_notebookTasks.clear();
    m_files.clear();
}

void TaskMgr::addAllTaskFolder()
{
    s_searchPaths.clear();
    auto &configMgr = ConfigMgr::getInst();
    // App scope task folder
    addSearchPath(configMgr.getAppTaskFolder());
    // User scope task folder
    addSearchPath(configMgr.getUserTaskFolder());
    // Notebook scope task folder
    auto path = getNotebookTaskFolder();
    if (!path.isNull()) addSearchPath(path);
}

void TaskMgr::loadAllTask()
{
    addAllTaskFolder();
    loadAvailableTasks();
    watchTaskEntrys();
    emit taskChanged();
}

void TaskMgr::watchTaskEntrys()
{
    clearWatchPaths();
    addWatchPaths(s_searchPaths);
    for (const auto &pa : s_searchPaths) {
        addWatchPaths(FileUtils::entryListRecursively(pa, QStringList(), QDir::AllDirs));
    }
    addWatchPaths(m_files);
}

void TaskMgr::loadAvailableTasks()
{
    m_files.clear();
    auto &configMgr = ConfigMgr::getInst();
    loadAvailableTasks(m_appTasks, configMgr.getAppTaskFolder());
    loadAvailableTasks(m_userTasks, configMgr.getUserTaskFolder());
    loadAvailableTasks(m_notebookTasks, getNotebookTaskFolder());
}

void TaskMgr::loadAvailableTasks(QVector<Task *> &p_tasks, const QString &p_searchPath)
{
    p_tasks.clear();
    if (p_searchPath.isEmpty()) return ;
    const auto taskFiles = FileUtils::entryListRecursively(p_searchPath, {"*.json"}, QDir::Files);
    for (auto &file : taskFiles) {
        m_files << file;
        checkAndAddTaskFile(p_tasks, file);
    }

    {
        QStringList list;
        for (auto task : p_tasks) {
            list << QFileInfo(task->getFile()).fileName();
        }
        if (!p_tasks.isEmpty()) qDebug() << "load tasks" << list;
    }
}

void TaskMgr::checkAndAddTaskFile(QVector<Task *> &p_tasks, const QString &p_file)
{
    QJsonDocument json;
    if (Task::isValidTaskFile(p_file, json)) {
        const auto localeStr = ConfigMgr::getInst().getCoreConfig().getLocaleToUse();
        p_tasks.push_back(Task::fromFile(p_file, json, localeStr, this));
    }
}

void TaskMgr::deleteTask(Task *p_task)
{
    Q_UNUSED(p_task);
}
