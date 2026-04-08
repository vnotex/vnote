#include "exportcontroller.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>
#include <exception>

#include <core/exception.h>
#include <core/servicelocator.h>
#include <core/services/filetypecoreservice.h>
#include <core/services/notebookcoreservice.h>
#include <export/exporter.h>
#include <utils/fileutils.h>

using namespace vnotex;

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
        if (!p_context.currentNodeId.isValid()) {
          emit logRequested(tr("No current buffer available for export."));
          break;
        }

        const auto relativePath = normalizedRelativePath(p_context.currentNodeId.relativePath);
        const auto filePath =
            notebookService->buildAbsolutePath(p_context.currentNodeId.notebookId, relativePath);
        if (filePath.isEmpty()) {
          emit logRequested(tr("Failed to resolve current buffer path."));
          break;
        }

        QString fileName = p_context.bufferName;
        if (fileName.isEmpty()) {
          fileName = QFileInfo(filePath).fileName();
        }

        const auto outputFile = exporter->doExportFile(
            p_option, p_context.bufferContent, filePath, fileName,
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

      case ExportSource::CurrentNote: {
        if (!p_context.currentNodeId.isValid()) {
          emit logRequested(tr("No current note available for export."));
          break;
        }

        const auto relativePath = normalizedRelativePath(p_context.currentNodeId.relativePath);
        const auto filePath =
            notebookService->buildAbsolutePath(p_context.currentNodeId.notebookId, relativePath);
        if (filePath.isEmpty()) {
          emit logRequested(tr("Failed to resolve current note path."));
          break;
        }

        const auto outputFile = exporter->doExportFile(
            p_option, FileUtils::readTextFile(filePath), filePath, QFileInfo(filePath).fileName(),
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
        collectExportFiles(p_context.currentFolderId.notebookId, relativePath, p_option.m_recursive,
                           p_option.m_exportAttachments, files);
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
        collectExportFiles(notebookId, QStringLiteral("."), p_option.m_recursive,
                           p_option.m_exportAttachments, files);
        outputFiles = exporter->doExportBatch(p_option, files, notebookBatchName(notebookId));
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
                                          QVector<ExportFileInfo> &p_files) {
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
    const auto name = fileObj.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
      continue;
    }

    const auto relativePath = folderPath.isEmpty() ? name : folderPath + QLatin1Char('/') + name;
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
    const auto name = folderObj.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) {
      continue;
    }

    const auto childFolderPath = folderPath.isEmpty() ? name : folderPath + QLatin1Char('/') + name;
    collectExportFiles(p_notebookId, childFolderPath, p_recursive, p_exportAttachments, p_files);
  }
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
  const auto rootFolder = config.value(QStringLiteral("rootFolder")).toString();
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
