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
        
        void setupTask(QTreeWidgetItem *p_item, const TaskMgr::TaskInfo &p_info);
        
        void loadTasks();
        
        void addTask(const TaskMgr::TaskInfo &p_info);
        
        QTreeWidget *m_taskExplorer = nullptr;
    };
} // ns vnotex

#endif // TASKPAGE_H
