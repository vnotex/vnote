#ifndef TASKPAGE_H
#define TASKPAGE_H

#include "settingspage.h"
#include "taskmgr.h"

class QTreeWidget;
class QTreeWidgetItem;

namespace vnotex
{
    class TaskPage : public SettingsPage
    {
        Q_OBJECT
    public:
        explicit TaskPage(QWidget *p_parent = nullptr);

        QString title() const Q_DECL_OVERRIDE;

    protected:
        void loadInternal() Q_DECL_OVERRIDE;

        void saveInternal() Q_DECL_OVERRIDE;

    private:
        void setupUI();
        
        void setupTask(QTreeWidgetItem *p_item, Task *p_task);
        
        void loadTasks();
        
        void addTask(Task *p_task, 
                     QTreeWidgetItem *p_parentItem = nullptr);
        
        Task *currentTask() const;
        
        QTreeWidget *m_taskExplorer = nullptr;
    };
} // ns vnotex

#endif // TASKPAGE_H
