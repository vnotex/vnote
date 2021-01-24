#ifndef TASKMGR_H
#define TASKMGR_H

#include <QObject>

#include <QVector>

#include "task.h"

namespace vnotex
{
    class TaskMgr : public QObject
    {
        Q_OBJECT
    public:
        
        explicit TaskMgr(QObject *parent = nullptr);
        
        const QVector<Task*> &getAllTasks() const;
        
        void refresh();
        
        void deleteTask(Task *p_task);
        
        static void addSearchPath(const QString &p_path);
        
    private:
        void loadAvailableTasks();
        
        void loadTasks(const QString &p_path);
        
        void checkAndAddTaskFile(const QString &p_file);
        
        QVector<Task*> m_tasks;
        
        // List of path to search for themes.
        static QStringList s_searchPaths;
    };
} // ns vnotex

#endif // TASKMGR_H
