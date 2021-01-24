#ifndef TASK_H
#define TASK_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QVector>
#include <QProcess>

class QAction;

namespace vnotex {

    class Task : public QObject
    {
        Q_OBJECT
    public:
        static Task* fromFile(const QString &p_file, QObject *p_parent = nullptr);
        
        static Task* fromJson(const QJsonObject &p_obj, QObject *p_parent = nullptr);
        
        QString label() const;
        
        QString filePath() const;
        
        QAction *runAction();
        
        const QVector<Task*> &subTasks() const;
        
        static bool isValidTaskFile(const QString &p_file);
        
        static Task* getTask(const QString &p_file);
        
    signals:
        void started();
        
        void errorOccurred(QProcess::ProcessError error);
        
    private:
        Task(QObject *p_parent = nullptr);
        
        void run();
        
        static QJsonObject readTaskFile(const QString &p_file);
        
        QString m_taskFilePath;
        
        QString m_label;
        
        QString m_command;
        
        QProcess *m_process = nullptr;
        
        QVector<Task*> m_subTasks;
    };
    
} // ns vnotex

#endif // TASK_H
