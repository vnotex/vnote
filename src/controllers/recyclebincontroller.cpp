#include "recyclebincontroller.h"

#include <QDir>
#include <QJsonObject>

#include <core/servicelocator.h>
#include <core/services/notebookservice.h>
using namespace vnotex;

RecycleBinController::RecycleBinController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

QString RecycleBinController::getRecycleBinPath(const QString &p_notebookId) const {
  if (p_notebookId.isEmpty()) {
    return QString();
  }

  auto *notebookService = m_services.get<NotebookService>();
  if (!notebookService) {
    return QString();
  }

  return notebookService->getRecycleBinPath(p_notebookId);
}

QString RecycleBinController::getNotebookName(const QString &p_notebookId) const {
  if (p_notebookId.isEmpty()) {
    return QString();
  }

  auto *notebookService = m_services.get<NotebookService>();
  if (!notebookService) {
    return QString();
  }

  QJsonObject config = notebookService->getNotebookConfig(p_notebookId);
  return config.value("name").toString();
}

RecycleBinResult RecycleBinController::prepareRecycleBinPath(const QString &p_notebookId) {
  RecycleBinResult result;

  QString recycleBinPath = getRecycleBinPath(p_notebookId);
  if (recycleBinPath.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("Recycle bin is not supported for this notebook type.");
    return result;
  }

  // Ensure the directory exists before opening.
  QDir dir(recycleBinPath);
  if (!dir.exists()) {
    if (!dir.mkpath(".")) {
      result.success = false;
      result.errorMessage = tr("Failed to create recycle bin folder.");
      return result;
    }
  }

  result.success = true;
  result.path = recycleBinPath;
  return result;
}

RecycleBinResult RecycleBinController::emptyRecycleBin(const QString &p_notebookId) {
  RecycleBinResult result;

  QString recycleBinPath = getRecycleBinPath(p_notebookId);
  if (recycleBinPath.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("Recycle bin is not supported for this notebook type.");
    return result;
  }

  auto *notebookService = m_services.get<NotebookService>();
  if (!notebookService) {
    result.success = false;
    result.errorMessage = tr("NotebookService not available.");
    return result;
  }

  notebookService->emptyRecycleBin(p_notebookId);
  result.success = true;
  return result;
}
