#ifndef TASKVARIABLEMGR_H
#define TASKVARIABLEMGR_H

#include <core/noncopyable.h>

#include <functional>

#include <QHash>
#include <QString>
#include <QSharedPointer>
#include <QScopedPointer>
#include <QProcessEnvironment>

namespace vnotex
{
    class Task;
    class Notebook;
    class Buffer;
    class ViewWindow;
    class TaskMgr;

    class TaskVariable
    {
    public:
        typedef std::function<QString(Task *, const QString &)> Func;

        TaskVariable(const QString &p_name, const Func &p_func);

        QString evaluate(Task *p_task, const QString &p_value) const;

    private:
        QString m_name;

        Func m_func;
    };


    class TaskVariableMgr : private Noncopyable
    {
    public:
        explicit TaskVariableMgr(TaskMgr *p_taskMgr);

        void init();

        QString evaluate(Task *p_task, const QString &p_text) const;

        QStringList evaluate(Task *p_task, const QStringList &p_texts) const;

        // Used for UT.
        void overrideVariable(const QString &p_name, const TaskVariable::Func &p_func);

        static Buffer *getCurrentBuffer();

        static QSharedPointer<Notebook> getCurrentNotebook();

    private:
        void initVariables();

        void initNotebookVariables();

        void initBufferVariables();

        void initTaskVariables();

        void initMagicVariables();

        void initEnvironmentVariables();

        void initConfigVariables();

        void initInputVariables();

        void initShellVariables();

        void addVariable(const QString &p_name, const TaskVariable::Func &p_func);

        const TaskVariable *findVariable(const QString &p_name) const;

        /*
        QString evaluateInputVariables(const QString &p_text,
                                       const Task *p_task) const;

        QString evaluateShellVariables(const QString &p_text,
                                       const Task *p_task) const;
       */

        static const ViewWindow *getCurrentViewWindow();

        TaskMgr *m_taskMgr = nullptr;

        QHash<QString, TaskVariable> m_variables;

        bool m_needUpdateSystemEnvironment = true;

        QProcessEnvironment m_systemEnv;

        // %{name[:value]}%.
        // Captured texts:
        // 1 - The name of the variable (trim needed).
        // 2 - The value option of the variable if available (trim needed).
        static const QString c_variableSymbolRegExp;
    };
} // ns vnotex

#endif // TASKVARIABLEMGR_H
