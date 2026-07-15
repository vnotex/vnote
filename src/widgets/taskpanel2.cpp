#include "taskpanel2.h"

#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <controllers/taskcontroller.h>
#include <core/servicelocator.h>
#include <core/services/task.h>
#include <core/services/taskservice.h>
#include <gui/services/themeservice.h>
#include <models/tasktreemodel.h>
#include <widgets/titlebar.h>

using namespace vnotex;

TaskPanel2::TaskPanel2(ServiceLocator &p_services, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services) {
  setupUI();
}

void TaskPanel2::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  setupTitleBar();
  mainLayout->addWidget(m_titleBar);

  m_controller = new TaskController(m_services, this);
  m_model = new TaskTreeModel(m_services.get<TaskService>(), this);

  m_treeView = new QTreeView(this);
  m_treeView->setModel(m_model);
  m_treeView->setHeaderHidden(true);
  m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
  m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
  m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_treeView->setExpandsOnDoubleClick(false);
  m_treeView->expandAll();
  mainLayout->addWidget(m_treeView, 1);

  connect(m_treeView, &QTreeView::activated, this, &TaskPanel2::onItemActivated);
  connect(m_treeView, &QTreeView::customContextMenuRequested, this,
          &TaskPanel2::onContextMenuRequested);

  connect(m_controller, &TaskController::errorOccurred, this, [this](const QString &p_msg) {
    QMessageBox::warning(window(), tr("Task"), p_msg);
  });

  // Rebuild the model whenever the task set changes (reload / notebook switch).
  auto *taskService = m_services.get<TaskService>();
  if (taskService) {
    connect(taskService, &TaskService::tasksUpdated, this, [this]() {
      m_model->refresh();
      m_treeView->expandAll();
    });
  }

  setFocusProxy(m_treeView);
}

void TaskPanel2::setupTitleBar() {
  m_titleBar =
      new TitleBar(m_services.get<ThemeService>(), QString(), false, TitleBar::Action::None, this);
  m_titleBar->setActionButtonsAlwaysShown(true);

  auto *newBtn = m_titleBar->addActionButton(QStringLiteral("add.svg"), tr("New Task"));
  connect(newBtn, &QToolButton::clicked, this, [this]() { m_controller->newTask(); });

  auto *openFolderBtn =
      m_titleBar->addActionButton(QStringLiteral("open_folder.svg"), tr("Open Task Folder"));
  connect(openFolderBtn, &QToolButton::clicked, this,
          [this]() { m_controller->openTaskFolder(); });

  auto *refreshBtn = m_titleBar->addActionButton(QStringLiteral("sync_now.svg"), tr("Refresh"));
  connect(refreshBtn, &QToolButton::clicked, this, [this]() { m_controller->refreshTasks(); });
}

void TaskPanel2::initialize() {
  m_model->refresh();
  m_treeView->expandAll();
}

void TaskPanel2::onItemActivated(const QModelIndex &p_index) {
  auto *task = m_model->taskForIndex(p_index);
  if (task) {
    m_controller->runTask(task);
  } else {
    // Group row: toggle expansion.
    m_treeView->setExpanded(p_index, !m_treeView->isExpanded(p_index));
  }
}

void TaskPanel2::onContextMenuRequested(const QPoint &p_pos) {
  const QModelIndex idx = m_treeView->indexAt(p_pos);
  if (!idx.isValid()) {
    return;
  }

  auto *task = m_model->taskForIndex(idx);
  if (!task) {
    return;
  }

  QMenu menu(this);

  menu.addAction(tr("Run"), this, [this, task]() { m_controller->runTask(task); });

  menu.addAction(tr("Open Task File"), this, [this, task]() { m_controller->openTaskFile(task); });

  // Delete removes the whole .json task file, so only offer it on top-level
  // task rows (a sub-task shares its parent's file).
  if (m_model->isTopLevelTask(idx)) {
    menu.addAction(tr("Delete Task File"), this, [this, task]() {
      const int ret = QMessageBox::question(
          window(), tr("Delete Task File"),
          tr("Delete the task file for \"%1\"?\n\nThis removes the file and any sub-tasks it "
             "contains: %2")
              .arg(task->getLabel(), task->getFile()),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (ret == QMessageBox::Yes) {
        m_controller->deleteTask(task);
      }
    });
  }

  menu.exec(m_treeView->mapToGlobal(p_pos));
}
