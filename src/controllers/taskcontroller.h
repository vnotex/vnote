#ifndef TASKCONTROLLER_H
#define TASKCONTROLLER_H

#include <QObject>
#include <QString>

namespace vnotex {

class ServiceLocator;
class TaskService;
class Task;

// Controller mediating file-based task operations for TaskPanel2. Owns no UI;
// runs tasks, opens task files/folders in the OS default handler, and
// creates/deletes task .json files, delegating data access to TaskService.
class TaskController : public QObject {
  Q_OBJECT

public:
  explicit TaskController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Run the given task (spawns its QProcess and streams output).
  void runTask(Task *p_task);

  // Reload all tasks from disk (app + notebook folders).
  void refreshTasks();

  // Open the app tasks folder in the OS file browser (created if missing).
  void openTaskFolder();

  // Create a starter task .json in the app tasks folder and reload tasks.
  // Returns the created file path, or "" on failure. The caller opens the file
  // for editing via the generic file-open flow.
  QString newTask();

  // Delete a task's .json file and reload tasks. Returns true on success.
  bool deleteTask(Task *p_task);

  // Absolute path of the app tasks folder (does not create it).
  QString getAppTaskFolderPath() const;

signals:
  void errorOccurred(const QString &p_message);

private:
  ServiceLocator &m_services;

  TaskService *m_taskService = nullptr;
};

} // namespace vnotex

#endif // TASKCONTROLLER_H
