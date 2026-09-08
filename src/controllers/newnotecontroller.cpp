#include "newnotecontroller.h"

#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QScopeGuard>
#include <QtConcurrent>

#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/snippetcoreservice.h>
#include <core/services/syncworkqueuemanager.h>
#include <utils/fileutils2.h>
#include <utils/pathutils.h>

#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

namespace {
QString encryptedCreationError(VxCoreError p_error) {
  if (p_error == VXCORE_ERR_ENCRYPTION_AUTH_FAILED) {
    return NewNoteController::tr("Unable to unlock: incorrect password or damaged key data");
  }
  if (p_error == VXCORE_ERR_ENCRYPTION_LOCKED) {
    return NewNoteController::tr("Unlock the notebook before creating an encrypted note.");
  }
  if (p_error == VXCORE_ERR_SYNC_IN_PROGRESS) {
    return NewNoteController::tr("The notebook is busy syncing. Retry after sync finishes.");
  }
  if (p_error == VXCORE_ERR_ENCRYPTION_RECOVERY_REQUIRED) {
    return NewNoteController::tr(
        "Encrypted note creation needs recovery. Restart VNote before editing or syncing.");
  }
  return QString::fromUtf8(vxcore_error_message(p_error));
}
} // namespace

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
  if (!result.valid || !p_input.encrypted) {
    return result;
  }
  const QString editorType = p_input.fileTypeName.toLower();
  if ((editorType != QLatin1String("markdown") && editorType != QLatin1String("text")) ||
      p_input.name.endsWith(QLatin1String(".vne"), Qt::CaseInsensitive)) {
    result.valid = false;
    result.message = tr("Encryption is available only for new Markdown or text notes.");
    return result;
  }
  auto *notebooks = m_services.get<NotebookCoreService>();
  if (!notebooks ||
      notebooks->getNotebookConfig(p_input.notebookId)
              .value(QLatin1String(vxcore::kJsonKeyType))
              .toString() != QLatin1String("bundled") ||
      notebooks->isNotebookReadOnly(p_input.notebookId)) {
    result.valid = false;
    result.message = tr("Encryption requires a writable managed notebook.");
    return result;
  }
  result = validateName(p_input.notebookId, p_input.parentFolderPath,
                        p_input.name + QStringLiteral(".vne"));
  return result;
}

NewNoteResult NewNoteController::createNote(const NewNoteInput &p_input,
                                            PreparedNotebookEncryption *p_setup) {
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

  if (p_input.encrypted) {
    return createEncryptedNote(p_input, p_setup);
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

NewNoteResult NewNoteController::createEncryptedNote(const NewNoteInput &p_input,
                                                     PreparedNotebookEncryption *p_setup) {
  NewNoteResult result;
  auto *notebooks = m_services.get<NotebookCoreService>();
  auto *buffers = m_services.get<BufferService>();
  auto *queues = m_services.get<SyncWorkQueueManager>();
  auto *gate = m_services.get<NotebookIoGate>();
  if (!notebooks || !buffers || !queues || !gate) {
    result.errorMessage = tr("The note encryption services are unavailable.");
    return result;
  }
  if (!buffers->beginProtectedOperation()) {
    result.errorMessage = encryptedCreationError(VXCORE_ERR_ENCRYPTION_LOCKED);
    return result;
  }
  const auto operation = qScopeGuard([&]() { buffers->endProtectedOperation(); });
  if (p_setup && !p_setup->isValid()) {
    result.errorMessage = p_setup->m_errorMessage.isEmpty()
                              ? encryptedCreationError(p_setup->m_error)
                              : p_setup->m_errorMessage;
    return result;
  }

  // Template overrides use the intended original filename/path, not the
  // ciphertext suffix. Both modes have the same UTF-8/caret contract as plain
  // creation, but neither calls createFile() or FileUtils2::writeFile().
  const QString relativePath = p_input.parentFolderPath.isEmpty()
                                   ? p_input.name
                                   : p_input.parentFolderPath + QLatin1Char('/') + p_input.name;
  QByteArray body;
  const auto clearBody = qScopeGuard([&]() { body.fill('\0'); });
  if (p_input.bodyMode == NewNoteBodyMode::LiteralContent) {
    body = p_input.literalContent.toUtf8();
    result.cursorOffset = p_input.literalContent.size();
  } else if (!p_input.templateContent.isEmpty()) {
    if (!m_services.get<SnippetCoreService>()) {
      result.errorMessage = tr("The template service is unavailable.");
      return result;
    }
    auto evaluated = evaluateTemplateContent(p_input.templateContent, p_input.name, relativePath);
    body = evaluated.content.toUtf8();
    result.cursorOffset = evaluated.cursorOffset;
    evaluated.content.fill(QChar(0));
  }

  auto maintenance = queues->tryAcquireMaintenance({p_input.notebookId});
  if (!maintenance) {
    result.errorMessage = encryptedCreationError(VXCORE_ERR_SYNC_IN_PROGRESS);
    return result;
  }
  QString fileId;
  QEventLoop loop;
  QFutureWatcher<VxCoreError> watcher;
  connect(&watcher, &QFutureWatcher<VxCoreError>::finished, &loop, &QEventLoop::quit);
  watcher.setFuture(QtConcurrent::run([&]() {
    NotebookIoGate::ScopedTryLock lock(*gate, p_input.notebookId, 5000);
    if (!lock.isLocked()) {
      return VXCORE_ERR_SYNC_IN_PROGRESS;
    }
    if (notebooks->isNotebookReadOnly(p_input.notebookId)) {
      return VXCORE_ERR_READ_ONLY;
    }
    // Recheck the destination after waiting for IO. Setup/KDF and user prompts
    // are already over; only key publication and ciphertext creation happen here.
    const auto validation = validateAll(p_input);
    if (!validation.valid) {
      result.errorMessage = validation.message;
      return VXCORE_ERR_INVALID_STATE;
    }
    if (p_setup) {
      const auto error = notebooks->commitNotebookEncryption(*p_setup);
      if (error != VXCORE_OK) {
        return error;
      }
    }
    return notebooks->createEncryptedNote(p_input.notebookId, p_input.parentFolderPath,
                                          p_input.name, p_input.fileTypeName.toLower(), body,
                                          &fileId);
  }));
  if (!watcher.isFinished()) {
    loop.exec();
  }
  watcher.waitForFinished();
  const VxCoreError error = watcher.result();
  if (error != VXCORE_OK) {
    if (result.errorMessage.isEmpty()) {
      result.errorMessage = encryptedCreationError(error);
    }
    return result;
  }

  // The core returns success only after durable publication, metadata update,
  // and journal cleanup. Use its stable identity rather than a plaintext path.
  result.nodeId = {p_input.notebookId, notebooks->getNodePathById(p_input.notebookId, fileId)};
  if (!result.nodeId.isValid() || result.nodeId.isRoot()) {
    result.errorMessage =
        tr("The encrypted note was created, but its identity could not be resolved. "
           "Reload the notebook before creating another copy.");
    return result;
  }
  result.success = true;
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
