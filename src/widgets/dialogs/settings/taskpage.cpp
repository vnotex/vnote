#include "taskpage.h"

#include <QGridLayout>
#include <QPushButton>

#include <widgets/treewidget.h>
#include <core/vnotex.h>
#include <core/taskmgr.h>

using namespace vnotex;

TaskPage::TaskPage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void TaskPage::setupUI()
{
    auto mainLayout = new QGridLayout(this);
    
    m_taskExplorer = new TreeWidget(this);
    TreeWidget::setupSingleColumnHeaderlessTree(m_taskExplorer, true, true);
    TreeWidget::showHorizontalScrollbar(m_taskExplorer);
    mainLayout->addWidget(m_taskExplorer, 0, 0, 3, 2);
    
    auto refreshBtn = new QPushButton(tr("Refresh"), this);
    mainLayout->addWidget(refreshBtn, 3, 0, 1, 1);
    
    auto openLocationBtn = new QPushButton(tr("Open Location"), this);
    mainLayout->addWidget(openLocationBtn, 3, 1, 1, 1);
    
    auto addBtn = new QPushButton(tr("Add"), this);
    mainLayout->addWidget(addBtn, 4, 0, 1, 1);
    
    auto deleteBtn = new QPushButton(tr("Delete"), this);
    mainLayout->addWidget(deleteBtn, 4, 1, 1, 1);
}

void TaskPage::setupTask(QTreeWidgetItem *p_item, TaskInfo *p_info)
{
    p_item->setText(0, p_info->m_displayName);
    p_item->setData(0, Qt::UserRole, QVariant::fromValue(p_info));
}

void TaskPage::loadTasks()
{
    const auto &taskMgr = VNoteX::getInst().getTaskMgr();
    const auto &tasks = taskMgr.getAllTasks();
    
    m_taskExplorer->clear();
    for (const auto &info : tasks) {
        addTask(info);
    }
}

void TaskPage::addTask(TaskInfo *p_info, 
                       QTreeWidgetItem *p_parentItem)
{
    QTreeWidgetItem *item;
    item = p_parentItem ? new QTreeWidgetItem(p_parentItem)
                        : new QTreeWidgetItem(m_taskExplorer);
    setupTask(item, p_info);
    for (auto subTask : p_info->m_subTask) {
        addTask(subTask, item);
    }
}

void TaskPage::loadInternal()
{
    loadTasks();
}

void TaskPage::saveInternal()
{
}

QString TaskPage::title() const
{
    return tr("Task");
}
