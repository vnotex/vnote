#include "newnotecontroller.h"

#include <QJsonObject>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <snippet/snippetmgr.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>

using namespace vnotex;

NewNoteController::NewNoteController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

NoteValidationResult NewNoteController::validateName(const QString &p_notebookId,
                                                     const QString &p_parentPath,
                                                     const QString &p_name) const {
  NoteValidationResult result;

  QString name = p_name.trimmed();
  if (name.isEmpty()) {
    result.valid = false;
    result.message = tr("Please specify a name for the note.");
    return result;
  }

  // Check if it's a legal filename.
  if (!PathUtils::isLegalFileName(name)) {
    result.valid = false;
    result.message = tr("Please specify a valid name for the note.");
    return result;
  }

  // Check for conflicts with existing files in the parent folder.
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (notebookService) {
    QJsonObject children = notebookService->listFolderChildren(p_notebookId, p_parentPath);
    QJsonArray files = children.value("files").toArray();
    for (const auto &file : files) {
      QJsonObject fileObj = file.toObject();
      QString existingName = fileObj.value("name").toString();
      if (existingName.compare(name, Qt::CaseInsensitive) == 0) {
        result.valid = false;
        result.message = tr("Name conflicts with existing note.");
        return result;
      }
    }
  }

  return result;
}

NoteValidationResult NewNoteController::validateAll(const NewNoteInput &p_input) const {
  NoteValidationResult result;

  if (p_input.notebookId.isEmpty()) {
    result.valid = false;
    result.message = tr("No notebook specified.");
    return result;
  }

  result = validateName(p_input.notebookId, p_input.parentFolderPath, p_input.name);
  return result;
}

NewNoteResult NewNoteController::createNote(const NewNoteInput &p_input) {
  NewNoteResult result;

  // Validate first.
  NoteValidationResult validation = validateAll(p_input);
  if (!validation.valid) {
    result.success = false;
    result.errorMessage = validation.message;
    return result;
  }

  // Get NotebookService.
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    result.success = false;
    result.errorMessage = tr("NotebookService not available.");
    return result;
  }

  // Create file via service (returns file ID, not path).
  QString fileId =
      notebookService->createFile(p_input.notebookId, p_input.parentFolderPath, p_input.name);

  if (fileId.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("Failed to create note (%1).").arg(p_input.name);
    return result;
  }

  // Get the relative path from the file ID.
  QString filePath = notebookService->getNodePathById(p_input.notebookId, fileId);
  if (filePath.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("Failed to get path for created note.");
    return result;
  }

  // Write template content if provided.
  if (!p_input.templateContent.isEmpty()) {
    QString evaluatedContent = evaluateTemplateContent(p_input.templateContent, p_input.name);

    // Get the full path and write content.
    QJsonObject notebookConfig = notebookService->getNotebookConfig(p_input.notebookId);
    QString rootPath = notebookConfig.value("rootFolder").toString();
    QString fullPath = PathUtils::concatenateFilePath(rootPath, filePath);
    FileUtils::writeFile(fullPath, evaluatedContent);
  }

  result.success = true;
  result.nodeId.notebookId = p_input.notebookId;
  result.nodeId.relativePath = filePath;
  return result;
}

QString NewNoteController::evaluateTemplateContent(const QString &p_content, const QString &p_name) {
  int cursorOffset = 0;
  return SnippetMgr::getInst().applySnippetBySymbol(p_content, QString(), cursorOffset,
                                                    SnippetMgr::generateOverrides(p_name));
}
