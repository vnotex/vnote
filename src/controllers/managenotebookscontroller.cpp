#include "managenotebookscontroller.h"

#include <QJsonDocument>

#include <core/servicelocator.h>
#include <core/services/notebookservice.h>

using namespace vnotex;
using vnotex::core::NotebookService;

ManageNotebooksController::ManageNotebooksController(ServiceLocator &p_services,
                                                     QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
}

QJsonArray ManageNotebooksController::listNotebooks() const {
  auto *notebookService = m_services.get<NotebookService>();
  return notebookService->listNotebooks();
}

NotebookInfo ManageNotebooksController::getNotebookInfo(const QString &p_notebookId) const {
  NotebookInfo info;

  if (p_notebookId.isEmpty()) {
    return info;
  }

  auto *notebookService = m_services.get<NotebookService>();
  QJsonObject config = notebookService->getNotebookConfig(p_notebookId);

  info.id = p_notebookId;
  info.name = config["name"].toString();
  info.description = config["description"].toString();
  info.rootFolder = config["rootFolder"].toString();
  info.type = config["type"].toString();

  // Map type to user-friendly display name.
  if (info.type == QStringLiteral("bundled")) {
    info.typeDisplayName = tr("Bundled Notebook");
  } else if (info.type == QStringLiteral("raw")) {
    info.typeDisplayName = tr("Raw Notebook");
  } else {
    info.typeDisplayName = info.type;
  }

  return info;
}

ManageNotebooksValidationResult ManageNotebooksController::validateName(
    const QString &p_name) const {
  ManageNotebooksValidationResult result;

  if (p_name.trimmed().isEmpty()) {
    result.valid = false;
    result.message = tr("Please specify a name for the notebook.");
    return result;
  }

  return result;
}

NotebookOperationResult ManageNotebooksController::updateNotebook(
    const NotebookUpdateInput &p_input) {
  NotebookOperationResult result;

  // Validate name first.
  auto validation = validateName(p_input.name);
  if (!validation.valid) {
    result.success = false;
    result.errorMessage = validation.message;
    return result;
  }

  if (p_input.notebookId.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("No notebook selected.");
    return result;
  }

  auto *notebookService = m_services.get<NotebookService>();

  // Read existing config, update only name and description, then write back.
  // vxcore does NOT support partial updates - missing fields get default values.
  // Note: rootFolder and type in the JSON are ignored by vxcore (not part of NotebookConfig).
  QJsonObject config = notebookService->getNotebookConfig(p_input.notebookId);
  config["name"] = p_input.name.trimmed();
  config["description"] = p_input.description;

  QString configJson = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
  if (!notebookService->updateNotebookConfig(p_input.notebookId, configJson)) {
    result.success = false;
    result.errorMessage = tr("Failed to update notebook configuration.");
    return result;
  }

  result.success = true;
  return result;
}

NotebookOperationResult ManageNotebooksController::closeNotebook(const QString &p_notebookId) {
  NotebookOperationResult result;

  if (p_notebookId.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("No notebook selected.");
    return result;
  }

  auto *notebookService = m_services.get<NotebookService>();
  if (!notebookService->closeNotebook(p_notebookId)) {
    result.success = false;
    result.errorMessage = tr("Failed to close notebook.");
    return result;
  }

  result.success = true;
  return result;
}
