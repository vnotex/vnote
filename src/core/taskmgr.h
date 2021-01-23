#ifndef TASKMGR_H
#define TASKMGR_H

#include <QObject>

#include <QVector>

#include "task.h"

namespace vnotex
{
    struct TaskInfo;
    typedef QVector<TaskInfo*> TaskInfoList;
    struct TaskInfo
    {
        // Id.
        QString m_name;
        
        // TODO
        // Locale supported.
        QString m_displayName;
        
        QString m_filePath;
        
        QVector<TaskInfo*> m_subTask;
    };

    class TaskMgr : public QObject
    {
        Q_OBJECT
    public:
        
        explicit TaskMgr(QObject *parent = nullptr);
        
        const TaskInfoList &getAllTasks() const;
        
        static void addSearchPath(const QString &p_path);
        
    private:
        void loadAvailableTasks();
        
        void loadTasks(const QString &p_path);
        
        void checkAndAddTaskFile(const QString &p_file, const QString &p_locale);
        
        TaskInfoList m_tasks;
        
        // List of path to search for themes.
        static QStringList s_searchPaths;
    };
} // ns vnotex

Q_DECLARE_METATYPE(vnotex::TaskInfo*);


#endif // TASKMGR_H
