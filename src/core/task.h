#ifndef TASK_H
#define TASK_H

#include <QString>
#include <QJsonObject>

namespace vnotex {

    struct TaskInfo;
    class Task
    {
    public:
        QString name() const;
        
        static bool isValidTaskFile(const QString &p_file);
        
        static Task *fromFile(const QString &p_file);
        
        static TaskInfo* getTaskInfo(const QString &p_file, 
                                       const QString &p_locale);
        
    private:
        static QJsonObject readTaskFile(const QString &p_file);
        
        Task(const QString &p_taskFilePath);
        QString m_taskFilePath;
        
        static TaskInfo* getTaskInfoFromTaskDescription(
                const QJsonObject &p_file, 
                const QString &p_locale);
    };
} // ns vnotex

#endif // TASK_H
