#include "taskcontroller.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <core/servicelocator.h>
#include <core/services/task.h>
#include <core/services/taskservice.h>

using namespace vnotex;

namespace {
// Minimal starter task. Runs a trivial shell command so the user immediately
// sees output, and demonstrates the label/command structure to edit.
const char *c_taskTemplate = R"({
    "version": "0.1.3",
    "type": "shell",
    "label": "New Task",
    "command": "echo Hello from VNote task"
}
)";
} // namespace

TaskController::TaskController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services),
      m_taskService(m_services.get<TaskService>()) {}

void TaskController::runTask(Task *p_task) {
  if (!p_task) {
    return;
  }
  if (m_taskService) {
    m_taskService->runTask(p_task);
  } else {
    p_task->run();
  }
}

void TaskController::refreshTasks() {
  if (m_taskService) {
    m_taskService->reload();
  }
}

QString TaskController::getAppTaskFolderPath() const {
  // Delegate to TaskService so the create/open folder and the load folder are
  // always derived in one place (avoids drift where new tasks land in a folder
  // the service never scans).
  return m_taskService ? m_taskService->getAppTaskFolder() : QString();
}

void TaskController::openTaskFolder() {
  const auto folder = getAppTaskFolderPath();
  if (folder.isEmpty()) {
    emit errorOccurred(tr("Task folder is not available."));
    return;
  }
  QDir dir(folder);
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
    emit errorOccurred(tr("Failed to create task folder: %1").arg(folder));
    return;
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void TaskController::openTaskFile(Task *p_task) {
  if (!p_task) {
    return;
  }
  const auto file = p_task->getFile();
  if (file.isEmpty() || !QFileInfo::exists(file)) {
    emit errorOccurred(tr("Task file does not exist: %1").arg(file));
    return;
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(file));
}

QString TaskController::newTask() {
  const auto folder = getAppTaskFolderPath();
  if (folder.isEmpty()) {
    emit errorOccurred(tr("Task folder is not available."));
    return QString();
  }

  QDir dir(folder);
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
    emit errorOccurred(tr("Failed to create task folder: %1").arg(folder));
    return QString();
  }

  // Pick a unique file name.
  QString fileName = QStringLiteral("task.json");
  if (dir.exists(fileName)) {
    fileName = QStringLiteral("task_%1.json")
                   .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss")));
  }
  const auto filePath = dir.absoluteFilePath(fileName);

  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    emit errorOccurred(tr("Failed to create task file: %1").arg(filePath));
    return QString();
  }
  file.write(c_taskTemplate);
  file.close();

  if (m_taskService) {
    m_taskService->reload();
  }

  QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
  return filePath;
}

bool TaskController::deleteTask(Task *p_task) {
  if (!p_task) {
    return false;
  }
  const auto file = p_task->getFile();
  if (file.isEmpty()) {
    emit errorOccurred(tr("Task file is not available."));
    return false;
  }

  if (QFileInfo::exists(file) && !QFile::remove(file)) {
    emit errorOccurred(tr("Failed to delete task file: %1").arg(file));
    return false;
  }

  if (m_taskService) {
    m_taskService->reload();
  }
  return true;
}
