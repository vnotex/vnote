#ifndef TASKVARIABLEMGR_H
#define TASKVARIABLEMGR_H

#include <functional>

#include <QHash>
#include <QString>
#include <QSharedPointer>

namespace vnotex {

class Task;
class TaskVariable;
class TaskVariableMgr;
class Notebook;

class TaskVariable {
public:
    enum Type
    {
        // Definition is plain text.
        Simple = 0,
    
        // Definition is a function call to get the value.
        FunctionBased,
    
        // Like FunctionBased, but should re-evaluate the value for each occurence.
        Dynamic,
        
        Invalid
    };
    typedef std::function<QString(const TaskVariable *,
                                  const Task*)> Func;
    TaskVariable(Type p_type,
                 const QString &p_name,
                 Func p_func = nullptr);
private:
    Type m_type;
    QString m_name;
    Func m_func;
};

class TaskVariableMgr
{
public:
    TaskVariableMgr();
    void refresh();
    QString evaluate(const QString &p_text, 
                     const Task *p_task) const;
    
    QStringList evaluate(const QStringList &p_list,
                         const Task *p_task) const;
    
private:
    void init();
    
    void add(TaskVariable::Type p_type,
             const QString &p_name,
             TaskVariable::Func p_func = nullptr);    
    
    QString evaluateInputVariables(const QString &p_text, 
                                   const Task *p_task) const;
    
    QString evaluateShellVariables(const QString &p_text,
                                   const Task *p_task) const;
    
    QHash<QString, TaskVariable> m_predefs;
    bool m_initialized;
};

} // ns vnotex

#endif // TASKVARIABLEMGR_H
