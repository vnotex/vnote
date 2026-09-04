#include "newnotecontroller.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/snippetcoreservice.h>
#include <utils/fileutils2.h>
#include <utils/pathutils.h>

#include <vxcore/notebook_json_keys.h>

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
      QString existingName = fileObj.value(QLatin1String(vxcore::kJsonKeyName)).toString();
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

  // Resolve the absolute path of the freshly created file.
  auto resolveFullPath = [&]() {
    QJsonObject notebookConfig = notebookService->getNotebookConfig(p_input.notebookId);
    QString rootPath = notebookConfig.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
    return PathUtils::concatenateFilePath(rootPath, filePath);
  };

  if (p_input.bodyMode == NewNoteBodyMode::LiteralContent) {
    // Captured text: written exactly as given, with no snippet/template
    // expansion. Written unconditionally so a cleared field yields an empty
    // note with an offset of zero.
    Error err = FileUtils2::writeFile(resolveFullPath(), p_input.literalContent.toUtf8());
    if (err) {
      qWarning() << err.what();
      result.success = false;
      result.errorMessage = tr("Failed to write note content.");
      return result;
    }
    // Qt caret offsets are UTF-16 positions, which is exactly QString::size().
    result.cursorOffset = p_input.literalContent.size();
  } else if (!p_input.templateContent.isEmpty()) {
    // Write template content if provided.
    EvaluatedTemplate evaluated =
        evaluateTemplateContent(p_input.templateContent, p_input.name, filePath);

    Error err = FileUtils2::writeFile(resolveFullPath(), evaluated.content.toUtf8());
    if (err) {
      qWarning() << err.what();
      result.success = false;
      result.errorMessage = tr("Failed to write note content.");
      return result;
    }
    result.cursorOffset = evaluated.cursorOffset;
  }

  result.success = true;
  result.nodeId.notebookId = p_input.notebookId;
  result.nodeId.relativePath = filePath;
  return result;
}

NewNoteResult NewNoteController::createQuickNote(const QuickNoteInput &p_input) {
  NewNoteResult result;

  if (p_input.notebookId.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("No notebook specified.");
    return result;
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  auto *snippetService = m_services.get<SnippetCoreService>();
  if (!notebookService || !snippetService) {
    result.success = false;
    result.errorMessage = tr("NotebookService not available.");
    return result;
  }

  // Expand the filename scheme (e.g. "%date%.md").
  QString expandedName = snippetService->applySnippetBySymbol(p_input.noteNameScheme);
  QFileInfo finfo(expandedName);

  // Ensure the (possibly newly expanded/date-based) target folder exists before
  // creating the file: vxcore createFile requires the parent folder node to exist.
  const QString folderPath = p_input.parentFolderPath;
  if (!folderPath.isEmpty()) {
    QString folderId = notebookService->createFolderPath(p_input.notebookId, folderPath);
    if (folderId.isEmpty()) {
      result.success = false;
      result.errorMessage = tr("Failed to create the quick note folder (%1).").arg(folderPath);
      return result;
    }
  }

  QJsonObject notebookConfig = notebookService->getNotebookConfig(p_input.notebookId);
  QString rootFolder = notebookConfig.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
  QString parentAbsPath = folderPath.isEmpty() ? rootFolder : QDir(rootFolder).filePath(folderPath);

  QString newFileName = FileUtils2::generateFileNameWithSequence(
      parentAbsPath, finfo.completeBaseName(), finfo.suffix());

  QString fileId = notebookService->createFile(p_input.notebookId, folderPath, newFileName);
  if (fileId.isEmpty()) {
    result.success = false;
    result.errorMessage = tr("Failed to create quick note (%1).").arg(newFileName);
    return result;
  }

  const QString relativePath =
      folderPath.isEmpty() ? newFileName : folderPath + QStringLiteral("/") + newFileName;

  if (!p_input.templateContent.isEmpty()) {
    // note/no overrides derive from the FINAL sequenced filename; folder derives from its path.
    EvaluatedTemplate evaluated =
        evaluateTemplateContent(p_input.templateContent, newFileName, relativePath);
    QString fullPath = QDir(parentAbsPath).filePath(newFileName);
    Error err = FileUtils2::writeFile(fullPath, evaluated.content.toUtf8());
    if (err) {
      qWarning() << err.what();
      result.success = false;
      result.errorMessage = tr("Failed to write note content.");
      return result;
    }
    result.cursorOffset = evaluated.cursorOffset;
  }

  result.success = true;
  result.nodeId.notebookId = p_input.notebookId;
  result.nodeId.relativePath = relativePath;
  return result;
}

EvaluatedTemplate NewNoteController::evaluateTemplateContent(const QString &p_content,
                                                             const QString &p_name,
                                                             const QString &p_relativePath) {
  // Provide magic-symbol overrides (%note%, %folder%, %no%) derived from the note destination,
  // mirroring the legacy SnippetMgr::generateOverrides(fileName). expandContent additionally
  // processes a top-level "@@" cursor mark and "$$" selection mark.
  QJsonObject overrides;
  overrides.insert(QStringLiteral("note"), p_name);
  overrides.insert(QStringLiteral("folder"),
                   NodeIdentifier{QString(), p_relativePath}.parentPath());
  overrides.insert(QStringLiteral("no"), QFileInfo(p_name).completeBaseName());

  QJsonObject r = m_services.get<SnippetCoreService>()->expandContent(p_content, overrides);

  EvaluatedTemplate evaluated;
  evaluated.content = r.value(QStringLiteral("text")).toString();
  evaluated.cursorOffset = r.value(QStringLiteral("cursorOffset")).toInt(-1);
  return evaluated;
}
