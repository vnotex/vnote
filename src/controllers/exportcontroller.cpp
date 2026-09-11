#include "exportcontroller.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTextCodec>
#include <QWidget>
#include <exception>
#include <utility>

#include <core/exception.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/workspacecoreservice.h>
#include <export/exporter.h>
#include <utils/fileutils2.h>
#include <utils/pathutils.h>

#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

namespace {

// Resolve the existing parent as well as an existing destination. QFileInfo alone
// cannot canonicalize a not-yet-created file, and a lexical prefix misses junctions.
QString canonicalExportDestination(const QString &p_path) {
  const QFileInfo info(p_path);
  if (!info.isAbsolute() || info.isSymLink()) {
    return {};
  }
  if (info.exists()) {
    return info.canonicalFilePath();
  }
  const auto parent = info.dir().canonicalPath();
  return parent.isEmpty() ? QString() : QDir(parent).filePath(info.fileName());
}

} // namespace

ExportController::ExportController(ServiceLocator &p_services, QObject *p_parent)
    : ExportController(p_services, nullptr, p_parent) {}

ExportController::ExportController(ServiceLocator &p_services, QWidget *p_widgetParent,
                                   QObject *p_parentObject)
    : QObject(p_parentObject), m_services(p_services), m_widgetParent(p_widgetParent) {}

void ExportController::doExport(const ExportOption &p_option, const ExportContext &p_context) {
  if (m_isExporting) {
    emit logRequested(tr("Export is already in progress."));
    return;
  }

  m_isExporting = true;

  QStringList outputFiles;

  do {
    auto *notebookService = m_services.get<NotebookCoreService>();
    if (!notebookService) {
      emit logRequested(tr("NotebookCoreService not available."));
      break;
    }

    auto *exporter = ensureExporter();
    if (!exporter) {
      emit logRequested(tr("Failed to create exporter."));
      break;
    }

    try {
      switch (p_option.m_source) {
      case ExportSource::CurrentBuffer: {
        if (isProtectedExportSource(p_context.currentNodeId, p_context.bufferPath)) {
          emit logRequested(tr("Protected content cannot use ordinary Export. Open the note and "
                               "choose Save Decrypted Copy."));
          break;
        }
        QString filePath;
        QString attachmentsFolder;
        if (p_context.currentNodeId.isValid()) {
          const auto relativePath = normalizedRelativePath(p_context.currentNodeId.relativePath);
          filePath =
              notebookService->buildAbsolutePath(p_context.currentNodeId.notebookId, relativePath);
          if (p_option.m_exportAttachments) {
            attachmentsFolder = notebookService->getAttachmentsFolder(
                p_context.currentNodeId.notebookId, relativePath);
          }
        } else {
          // External file: no notebook, therefore no attachments folder.
          filePath = p_context.bufferPath;
        }

        if (filePath.isEmpty()) {
          emit logRequested(tr("No current buffer available for export."));
          break;
        }

        QString fileName = p_context.bufferName;
        if (fileName.isEmpty()) {
          fileName = QFileInfo(filePath).fileName();
        }

        const auto outputFile = exporter->doExportFile(p_option, p_context.bufferContent, filePath,
                                                       fileName, QFileInfo(filePath).absolutePath(),
                                                       attachmentsFolder, isMarkdownFile(filePath));
        if (!outputFile.isEmpty()) {
          outputFiles.append(outputFile);
        }
        break;
      }

      case ExportSource::CurrentNote: {
        if (!p_context.currentNodeId.isValid()) {
          emit logRequested(tr("No current note available for export."));
          break;
        }

        if (isProtectedExportSource(p_context.currentNodeId, QString())) {
          emit logRequested(tr("Protected content cannot use ordinary Export. Open the note and "
                               "choose Save Decrypted Copy."));
          break;
        }

        const auto relativePath = normalizedRelativePath(p_context.currentNodeId.relativePath);
        const auto filePath =
            notebookService->buildAbsolutePath(p_context.currentNodeId.notebookId, relativePath);
        if (filePath.isEmpty()) {
          emit logRequested(tr("Failed to resolve current note path."));
          break;
        }

        QString fileContent;
        {
          Error err = FileUtils2::readTextFile(filePath, &fileContent);
          if (err) {
            qWarning() << err.what();
            emit logRequested(tr("Failed to read current note content."));
            break;
          }
        }
        const auto outputFile = exporter->doExportFile(
            p_option, fileContent, filePath, QFileInfo(filePath).fileName(),
            QFileInfo(filePath).absolutePath(),
            p_option.m_exportAttachments ? notebookService->getAttachmentsFolder(
                                               p_context.currentNodeId.notebookId, relativePath)
                                         : QString(),
            isMarkdownFile(filePath));
        if (!outputFile.isEmpty()) {
          outputFiles.append(outputFile);
        }
        break;
      }

      case ExportSource::CurrentFolder: {
        if (!p_context.currentFolderId.isValid()) {
          emit logRequested(tr("No current folder available for export."));
          break;
        }

        const auto relativePath = normalizedRelativePath(p_context.currentFolderId.relativePath);
        QVector<ExportFileInfo> files;
        QStringList protectedFiles;
        collectExportFiles(p_context.currentFolderId.notebookId, relativePath, p_option.m_recursive,
                           p_option.m_exportAttachments, files, protectedFiles);
        if (refuseProtectedBatch(protectedFiles)) {
          break;
        }
        outputFiles = exporter->doExportBatch(
            p_option, files, folderBatchName(p_context.currentFolderId.notebookId, relativePath));
        break;
      }

      case ExportSource::CurrentNotebook: {
        QString notebookId = p_context.notebookId;
        if (notebookId.isEmpty()) {
          notebookId = p_context.currentNodeId.notebookId;
        }

        if (notebookId.isEmpty()) {
          emit logRequested(tr("No current notebook available for export."));
          break;
        }

        QVector<ExportFileInfo> files;
        QStringList protectedFiles;
        collectExportFiles(notebookId, QStringLiteral("."), p_option.m_recursive,
                           p_option.m_exportAttachments, files, protectedFiles);
        if (refuseProtectedBatch(protectedFiles)) {
          break;
        }
        outputFiles = exporter->doExportBatch(p_option, files, notebookBatchName(notebookId));
        break;
      }

      case ExportSource::Workspace: {
        if (p_option.m_workspaceId.isEmpty()) {
          emit logRequested(tr("No workspace selected for export."));
          break;
        }

        QVector<ExportFileInfo> files;
        QStringList protectedFiles;
        collectWorkspaceFiles(p_option.m_workspaceId, p_option.m_exportAttachments, files,
                              protectedFiles);
        if (refuseProtectedBatch(protectedFiles)) {
          break;
        }
        if (files.isEmpty()) {
          emit logRequested(tr("Workspace has no exportable buffers."));
          break;
        }
        outputFiles =
            exporter->doExportBatch(p_option, files, workspaceBatchName(p_option.m_workspaceId));
        break;
      }

      default:
        emit logRequested(tr("Unsupported export source."));
        break;
      }
    } catch (const Exception &p_e) {
      emit logRequested(QString::fromUtf8(p_e.what()));
    } catch (const std::exception &p_e) {
      emit logRequested(QString::fromUtf8(p_e.what()));
    }
  } while (false);

  m_isExporting = false;
  emit exportFinished(outputFiles);
}

void ExportController::stop() {
  if (m_exporter) {
    m_exporter->stop();
  }
}

bool ExportController::isExporting() const { return m_isExporting; }

Exporter *ExportController::ensureExporter() {
  if (!m_exporter) {
    m_exporter = new Exporter(m_services, m_widgetParent.data());
    connect(m_exporter, &Exporter::progressUpdated, this, &ExportController::progressUpdated);
    connect(m_exporter, &Exporter::logRequested, this, &ExportController::logRequested);
  }

  return m_exporter;
}

void ExportController::collectExportFiles(const QString &p_notebookId, const QString &p_folderPath,
                                          bool p_recursive, bool p_exportAttachments,
                                          QVector<ExportFileInfo> &p_files,
                                          QStringList &p_protectedFiles) {
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    emit logRequested(tr("NotebookCoreService not available."));
    return;
  }

  const auto folderPath = normalizedRelativePath(p_folderPath);
  const auto children = notebookService->listFolderChildren(p_notebookId, folderPath);

  const auto fileArray = children.value(QStringLiteral("files")).toArray();
  for (const auto &fileValue : fileArray) {
    const auto fileObj = fileValue.toObject();
    const auto name = fileObj.value(QLatin1String(vxcore::kJsonKeyName)).toString();
    if (name.isEmpty()) {
      continue;
    }

    const auto relativePath = folderPath.isEmpty() ? name : folderPath + QLatin1Char('/') + name;
    if (name.endsWith(QLatin1String(".vne"), Qt::CaseInsensitive) ||
        fileObj.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool() ||
        fileObj.value(QLatin1String(vxcore::kJsonKeyMetadata))
            .toObject()
            .value(QLatin1String(vxcore::kJsonKeyEncrypted))
            .toBool()) {
      p_protectedFiles.append(relativePath);
      continue;
    }
    const auto filePath = notebookService->buildAbsolutePath(p_notebookId, relativePath);
    if (filePath.isEmpty()) {
      emit logRequested(tr("Failed to resolve file path for (%1).").arg(relativePath));
      continue;
    }

    ExportFileInfo info;
    info.filePath = filePath;
    info.fileName = name;
    info.resourcePath = QFileInfo(filePath).absolutePath();
    info.attachmentFolderPath =
        p_exportAttachments ? notebookService->getAttachmentsFolder(p_notebookId, relativePath)
                            : QString();
    info.isMarkdown = isMarkdownFile(filePath);
    p_files.append(info);
  }

  if (!p_recursive) {
    return;
  }

  const auto folderArray = children.value(QStringLiteral("folders")).toArray();
  for (const auto &folderValue : folderArray) {
    const auto folderObj = folderValue.toObject();
    const auto name = folderObj.value(QLatin1String(vxcore::kJsonKeyName)).toString();
    if (name.isEmpty()) {
      continue;
    }

    const auto childFolderPath = folderPath.isEmpty() ? name : folderPath + QLatin1Char('/') + name;
    collectExportFiles(p_notebookId, childFolderPath, p_recursive, p_exportAttachments, p_files,
                       p_protectedFiles);
  }
}

bool ExportController::isExportableNode(const NodeIdentifier &p_nodeId) {
  if (p_nodeId.relativePath.isEmpty() || p_nodeId.relativePath == QStringLiteral(".") ||
      p_nodeId.isVirtual()) {
    return false;
  }
  // An empty notebookId means an external file, whose relativePath is an ABSOLUTE path
  // (MainWindow2::doOpenFiles). Requiring that keeps a malformed or extension-created
  // NodeIdentifier{"", "draft.md"} from being resolved against the process working directory.
  if (p_nodeId.notebookId.isEmpty()) {
    return QDir::isAbsolutePath(p_nodeId.relativePath);
  }
  return true;
}

void ExportController::collectWorkspaceFiles(const QString &p_workspaceId, bool p_exportAttachments,
                                             QVector<ExportFileInfo> &p_files,
                                             QStringList &p_protectedFiles) {
  auto *workspaceService = m_services.get<WorkspaceCoreService>();
  auto *bufferService = m_services.get<BufferService>();
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!workspaceService || !bufferService || !notebookService) {
    emit logRequested(tr("Required services not available for workspace export."));
    return;
  }

  const auto wsConfig = workspaceService->getWorkspace(p_workspaceId);
  const auto bufferIds = wsConfig.value(QStringLiteral("bufferIds")).toArray();
  for (const auto &bufferValue : bufferIds) {
    const auto bufferId = bufferValue.toString();
    if (bufferId.isEmpty()) {
      continue;
    }

    // Resolve the buffer id to notebookId + relativePath WITHOUT opening it.
    const Buffer2 buffer = bufferService->getBufferHandle(bufferId);
    if (!buffer.isValid()) {
      continue;
    }

    const auto &nodeId = buffer.nodeId();
    // Skip virtual/unsaved buffers (e.g. vx://home).
    if (!isExportableNode(nodeId)) {
      continue;
    }
    const auto relativePath = normalizedRelativePath(nodeId.relativePath);
    if (buffer.isEncrypted() || relativePath.endsWith(QLatin1String(".vne"), Qt::CaseInsensitive)) {
      p_protectedFiles.append(relativePath);
      continue;
    }

    QString filePath;
    QString attachmentFolderPath;
    if (!nodeId.notebookId.isEmpty()) {
      filePath = notebookService->buildAbsolutePath(nodeId.notebookId, relativePath);
      if (p_exportAttachments) {
        attachmentFolderPath =
            notebookService->getAttachmentsFolder(nodeId.notebookId, relativePath);
      }
    } else {
      filePath = buffer.resolvedPath();
    }
    if (filePath.isEmpty()) {
      emit logRequested(tr("Failed to resolve file path for (%1).").arg(relativePath));
      continue;
    }

    ExportFileInfo info;
    info.filePath = filePath;
    info.fileName = QFileInfo(filePath).fileName();
    info.resourcePath = QFileInfo(filePath).absolutePath();
    info.attachmentFolderPath = attachmentFolderPath;
    info.isMarkdown = isMarkdownFile(filePath);
    p_files.append(info);
  }
}

bool ExportController::isProtectedExportSource(const NodeIdentifier &p_nodeId,
                                               const QString &p_path) const {
  if (p_path.endsWith(QLatin1String(".vne"), Qt::CaseInsensitive) ||
      p_nodeId.relativePath.endsWith(QLatin1String(".vne"), Qt::CaseInsensitive)) {
    return true;
  }
  if (auto *buffers = m_services.get<BufferService>()) {
    if (buffers->findOpenProtectedBuffer(p_nodeId).isValid()) {
      return true;
    }
  }
  if (p_nodeId.isValid()) {
    if (auto *notebooks = m_services.get<NotebookCoreService>()) {
      VxCoreError error;
      const auto info = notebooks->getFileInfo(p_nodeId.notebookId, p_nodeId.relativePath, &error);
      return error != VXCORE_OK || info.value(QLatin1String(vxcore::kJsonKeyEncrypted)).toBool() ||
             info.value(QLatin1String(vxcore::kJsonKeyMetadata))
                 .toObject()
                 .value(QLatin1String(vxcore::kJsonKeyEncrypted))
                 .toBool();
    }
  }
  return false;
}

bool ExportController::refuseProtectedBatch(const QStringList &p_protectedFiles) {
  if (p_protectedFiles.isEmpty()) {
    return false;
  }
  emit logRequested(tr("Export refused: this selection contains protected notes. "
                       "Use Save Decrypted Copy on each note; no files were exported.\n%1")
                        .arg(p_protectedFiles.join(QLatin1Char('\n'))));
  return true;
}

QString ExportController::decryptedNoteName(const Buffer2 &p_buffer) {
  const auto editor = p_buffer.editorType();
  if (editor != QLatin1String("markdown") && editor != QLatin1String("text") &&
      editor != QLatin1String("mindmap")) {
    return {};
  }
  auto name = QFileInfo(p_buffer.nodeId().relativePath).fileName();
  if (name.endsWith(QLatin1String(".vne"), Qt::CaseInsensitive)) {
    name.chop(4);
  }
  return PathUtils::isLegalFileName(name) ? name : QString();
}

bool ExportController::isDecryptedCopyDestinationAllowed(const Buffer2 &p_buffer,
                                                         const QString &p_destination) const {
  auto *notebooks = m_services.get<NotebookCoreService>();
  if (!notebooks || !p_buffer.isValid() || !p_buffer.isEncrypted() ||
      p_buffer.nodeId().notebookId.isEmpty()) {
    return false;
  }
  const auto root = QFileInfo(notebooks->buildAbsolutePath(p_buffer.nodeId().notebookId, QString()))
                        .canonicalFilePath();
  const auto destination = canonicalExportDestination(p_destination);
  return !root.isEmpty() && !destination.isEmpty() && !PathUtils::pathContains(root, destination);
}

VxCoreError ExportController::saveDecryptedCopy(const Buffer2 &p_buffer,
                                                const QString &p_destination,
                                                const QString &p_content) {
  if (!p_buffer.isValid() || !p_buffer.isEncrypted()) {
    return VXCORE_ERR_INVALID_STATE;
  }
  const auto lease = p_buffer.acquireProtectedLease();
  if (!lease) {
    return VXCORE_ERR_ENCRYPTION_LOCKED;
  }
  // A read-only source may be deliberately exported; it is never mutated.
  if (!isDecryptedCopyDestinationAllowed(p_buffer, p_destination) ||
      QFileInfo(p_destination).isDir()) {
    return VXCORE_ERR_INVALID_PARAM;
  }
  if (decryptedNoteName(p_buffer).isEmpty()) {
    return VXCORE_ERR_UNSUPPORTED;
  }
  // Authenticate the source even when exporting a live editor snapshot. Never
  // read the encrypted path as text or turn an authentication error into an empty note.
  VxCoreError error;
  auto body = p_buffer.getContentRaw(&error);
  const auto clearBody = qScopeGuard([&]() { body.fill('\0'); });
  if (error != VXCORE_OK) {
    return error;
  }
  auto originalText = p_buffer.decode(QByteArrayViewCompat(body));
  const auto clearOriginalText = qScopeGuard([&]() { originalText.fill(QChar::Null); });
  // An unchanged snapshot retains its exact original encoding, BOM and line endings.
  if (p_content != originalText) {
    auto *codec = QTextCodec::codecForName(p_buffer.encoding().toUtf8());
    if (!codec) {
      return VXCORE_ERR_UNSUPPORTED;
    }
    QTextCodec::ConverterState state;
    auto encoded = codec->fromUnicode(p_content.constData(), p_content.size(), &state);
    if (state.invalidChars || state.remainingChars) {
      encoded.fill('\0');
      return VXCORE_ERR_UNSUPPORTED;
    }
    body.fill('\0');
    body = std::move(encoded);
  }
  if (!lease->isCurrent()) {
    return VXCORE_ERR_ENCRYPTION_LOCKED;
  }
  const auto path = canonicalExportDestination(p_destination);
  if (!isDecryptedCopyDestinationAllowed(p_buffer, path)) {
    return VXCORE_ERR_INVALID_PARAM;
  }
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly) || file.write(body) != body.size() || !file.commit()) {
    return VXCORE_ERR_IO;
  }
  return VXCORE_OK;
}

bool ExportController::isMarkdownFile(const QString &p_filePath) const {
  auto *fileTypeService = m_services.get<FileTypeCoreService>();
  if (!fileTypeService) {
    return false;
  }

  const auto suffix = QFileInfo(p_filePath).suffix().toLower();
  return fileTypeService->getFileTypeBySuffix(suffix).isMarkdown();
}

QString ExportController::normalizedRelativePath(const QString &p_relativePath) const {
  return p_relativePath == QStringLiteral(".") ? QString() : p_relativePath;
}

QString ExportController::notebookBatchName(const QString &p_notebookId) const {
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService) {
    return p_notebookId;
  }

  const auto config = notebookService->getNotebookConfig(p_notebookId);
  const auto rootFolder = config.value(QLatin1String(vxcore::kJsonKeyRootFolder)).toString();
  const auto name = QFileInfo(rootFolder).fileName();
  return name.isEmpty() ? p_notebookId : name;
}

QString ExportController::folderBatchName(const QString &p_notebookId,
                                          const QString &p_folderPath) const {
  const auto folderPath = normalizedRelativePath(p_folderPath);
  if (folderPath.isEmpty()) {
    return notebookBatchName(p_notebookId);
  }

  const auto name = QFileInfo(folderPath).fileName();
  return name.isEmpty() ? notebookBatchName(p_notebookId) : name;
}

QString ExportController::workspaceBatchName(const QString &p_workspaceId) const {
  // Reduce arbitrary text to a single safe filename leaf: strip path separators /
  // drive markers / platform-invalid characters and ASCII control chars, drop
  // trailing dots/spaces, and reject pure-dot names and Windows reserved device
  // names so the result can never escape or break the selected output directory.
  const auto sanitizeLeaf = [](const QString &p_raw) -> QString {
    QString leaf = p_raw;
    leaf.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    leaf.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1f]")));
    leaf = leaf.trimmed();
    while (leaf.endsWith(QLatin1Char('.')) || leaf.endsWith(QLatin1Char(' '))) {
      leaf.chop(1);
    }
    leaf = leaf.trimmed();
    if (leaf.isEmpty() || leaf == QStringLiteral(".") || leaf == QStringLiteral("..")) {
      return QString();
    }
    static const QRegularExpression reserved(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$"),
        QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(leaf).hasMatch()) {
      return QString();
    }
    return leaf;
  };

  const QString fallback = sanitizeLeaf(QStringLiteral("workspace-") + p_workspaceId);

  auto *workspaceService = m_services.get<WorkspaceCoreService>();
  if (workspaceService) {
    const auto wsConfig = workspaceService->getWorkspace(p_workspaceId);
    const QString name = sanitizeLeaf(wsConfig.value(QStringLiteral("name")).toString());
    if (!name.isEmpty()) {
      return name;
    }
  }

  return fallback.isEmpty() ? QStringLiteral("workspace") : fallback;
}
