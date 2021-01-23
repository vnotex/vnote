#ifndef BUILDMGR_H
#define BUILDMGR_H

#include <QObject>

#include <QVector>

#include "task.h"

namespace vnotex
{

    class TaskMgr : public QObject
    {
        Q_OBJECT
    public:
        struct TaskInfo
        {
            // Id.
            QString m_name;
            
            // Locale supported.
            QString m_displayName;
            
            QString m_filePath;
        };
        
        explicit TaskMgr(QObject *parent = nullptr);
        
        const QVector<TaskInfo> &getAllTasks() const;
        
        static void addSearchPath(const QString &p_path);
        
    private:
        void loadAvailableTasks();
        
        void loadTasks(const QString &p_path);
        
        void checkAndAddTaskFile(const QString &p_file, const QString &p_locale);
        
        QVector<TaskInfo> m_tasks;
        
        // List of path to search for themes.
        static QStringList s_searchPaths;
    };
}

Q_DECLARE_METATYPE(vnotex::TaskMgr::TaskInfo);


#endif // BUILDMGR_H
