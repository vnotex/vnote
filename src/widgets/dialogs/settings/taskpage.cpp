#include "taskpage.h"

#include <QGridLayout>
#include <QPushButton>
#include <QFileInfo>

#include <widgets/treewidget.h>
#include <core/vnotex.h>
#include <core/taskmgr.h>
#include <utils/widgetutils.h>

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
    connect(openLocationBtn, &QPushButton::clicked,
            this, [this]() {
        auto task = currentTask();
        if (task) {
            auto path = QFileInfo(task->filePath()).absolutePath();
            WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(path));
        }
    });
    
    auto addBtn = new QPushButton(tr("Add"), this);
    mainLayout->addWidget(addBtn, 4, 0, 1, 1);
    
    auto deleteBtn = new QPushButton(tr("Delete"), this);
    mainLayout->addWidget(deleteBtn, 4, 1, 1, 1);
}

void TaskPage::setupTask(QTreeWidgetItem *p_item, Task *p_task)
{
    p_item->setText(0, p_task->label());
    p_item->setData(0, Qt::UserRole, 
                    QVariant::fromValue(qobject_cast<QObject*>(p_task)));
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

void TaskPage::addTask(Task *p_task, 
                       QTreeWidgetItem *p_parentItem)
{
    QTreeWidgetItem *item;
    item = p_parentItem ? new QTreeWidgetItem(p_parentItem)
                        : new QTreeWidgetItem(m_taskExplorer);
    setupTask(item, p_task);
    for (auto subTask : p_task->subTasks()) {
        addTask(subTask, item);
    }
}

Task *TaskPage::currentTask() const
{
    auto item = m_taskExplorer->currentItem();
    while (item && item->parent()) {
        item = item->parent();   
    }
    if (item) {
        auto data = item->data(0, Qt::UserRole).value<QObject*>();
        return qobject_cast<Task*>(data);
    }
    return nullptr;
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
