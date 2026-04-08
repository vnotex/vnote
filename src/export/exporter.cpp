#include "exporter.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QWidget>

#include "webviewexporter.h"
#include <utils/contentmediautils.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <utils/processutils.h>

using namespace vnotex;

Exporter::Exporter(ServiceLocator &p_services, QWidget *p_parent)
    : QObject(p_parent), m_services(p_services) {}

static QString makeOutputFolder(const QString &p_outputDir, const QString &p_folderName) {
  const auto name = FileUtils::generateFileNameWithSequence(p_outputDir, p_folderName);
  const auto outputFolder = PathUtils::concatenateFilePath(p_outputDir, name);
  if (!QDir().mkpath(outputFolder)) {
    return QString();
  }

  return outputFolder;
}

QString Exporter::doExportFile(const ExportOption &p_option, const QString &p_content,
                               const QString &p_filePath, const QString &p_fileName,
                               const QString &p_resourcePath, const QString &p_attachmentFolderPath,
                               bool p_isMarkdown) {
  m_askedToStop = false;

  if (!QDir().mkpath(p_option.m_outputDir)) {
    emit logRequested(tr("Failed to create output folder (%1).").arg(p_option.m_outputDir));
    return QString();
  }

  ExportFileInfo fileInfo;
  fileInfo.filePath = p_filePath;
  fileInfo.fileName = p_fileName;
  fileInfo.resourcePath = p_resourcePath;
  fileInfo.attachmentFolderPath = p_attachmentFolderPath;
  fileInfo.isMarkdown = p_isMarkdown;

  auto outputFile = doExport(p_option, p_option.m_outputDir, fileInfo, p_content);
  cleanUp();
  return outputFile;
}

QStringList Exporter::doExportBatch(const ExportOption &p_option,
                                    const QVector<ExportFileInfo> &p_files,
                                    const QString &p_batchName) {
  m_askedToStop = false;

  QStringList outputFiles;

  if (!QDir().mkpath(p_option.m_outputDir)) {
    emit logRequested(tr("Failed to create output folder (%1).").arg(p_option.m_outputDir));
    return outputFiles;
  }

  if (p_option.m_targetFormat == ExportFormat::PDF && p_option.m_pdfOption.m_useWkhtmltopdf &&
      p_option.m_pdfOption.m_allInOne) {
    auto file = doExportPdfAllInOne(p_option, p_files, p_batchName);
    if (!file.isEmpty()) {
      outputFiles << file;
    }
  } else if (p_option.m_targetFormat == ExportFormat::Custom && p_option.m_customOption &&
             p_option.m_customOption->m_allInOne) {
    auto file = doExportCustomAllInOne(p_option, p_files, p_batchName);
    if (!file.isEmpty()) {
      outputFiles << file;
    }
  } else {
    const auto outputFolder = makeOutputFolder(p_option.m_outputDir, p_batchName);
    if (outputFolder.isEmpty()) {
      emit logRequested(tr("Failed to create output folder under (%1).").arg(p_option.m_outputDir));
      return outputFiles;
    }

    emit progressUpdated(0, p_files.size());
    for (int i = 0; i < p_files.size(); ++i) {
      if (checkAskedToStop()) {
        break;
      }

      const auto outputFile = doExport(p_option, outputFolder, p_files[i]);
      if (!outputFile.isEmpty()) {
        outputFiles << outputFile;
      }

      emit progressUpdated(i + 1, p_files.size());
    }
  }

  cleanUp();
  return outputFiles;
}

QString Exporter::doExport(const ExportOption &p_option, const QString &p_outputDir,
                           const ExportFileInfo &p_file, const QString &p_content) {
  QString outputFile;

  switch (p_option.m_targetFormat) {
  case ExportFormat::Markdown:
    outputFile =
        doExportMarkdown(p_option, p_outputDir, p_content, p_file.filePath, p_file.fileName,
                         p_file.resourcePath, p_file.attachmentFolderPath);
    break;

  case ExportFormat::HTML:
    outputFile = doExportHtml(p_option, p_outputDir, p_content, p_file.filePath, p_file.fileName,
                              p_file.resourcePath, QString(), p_file.attachmentFolderPath);
    break;

  case ExportFormat::PDF:
    outputFile = doExportPdf(p_option, p_outputDir, p_content, p_file.filePath, p_file.fileName,
                             p_file.resourcePath, QString(), p_file.attachmentFolderPath);
    break;

  case ExportFormat::Custom:
    outputFile = doExportCustom(p_option, p_outputDir, p_content, p_file.filePath, p_file.fileName,
                                p_file.resourcePath, QString(), p_file.attachmentFolderPath);
    break;

  default:
    emit logRequested(
        tr("Unknown target format %1.").arg(exportFormatString(p_option.m_targetFormat)));
    break;
  }

  const auto sourceFile = p_file.filePath.isEmpty() ? p_file.fileName : p_file.filePath;
  if (!outputFile.isEmpty()) {
    emit logRequested(tr("File (%1) exported to (%2)").arg(sourceFile, outputFile));
  } else {
    emit logRequested(tr("Failed to export file (%1)").arg(sourceFile));
  }

  return outputFile;
}

QString Exporter::doExportMarkdown(const ExportOption &p_option, const QString &p_outputDir,
                                   const QString &p_content, const QString &p_filePath,
                                   const QString &p_fileName, const QString &p_resourcePath,
                                   const QString &p_attachmentFolderPath) {
  Q_UNUSED(p_option);

  auto outputName = p_fileName;
  if (outputName.isEmpty()) {
    outputName = QFileInfo(p_filePath).fileName();
  }

  const auto name = FileUtils::generateFileNameWithSequence(p_outputDir, outputName, "");
  const auto outputFolder = PathUtils::concatenateFilePath(p_outputDir, name);
  QDir outDir(outputFolder);
  if (!outDir.mkpath(outputFolder)) {
    emit logRequested(tr("Failed to create output folder under (%1).").arg(p_outputDir));
    return QString();
  }

  auto destFilePath = outDir.filePath(outputName);
  if (!p_content.isEmpty()) {
    FileUtils::writeFile(destFilePath, p_content);
  } else if (!p_filePath.isEmpty()) {
    FileUtils::copyFile(p_filePath, destFilePath, false);
  } else {
    emit logRequested(tr("Failed to export markdown due to empty content and file path."));
    return QString();
  }

  const auto mediaSourceFile = p_filePath.isEmpty() ? destFilePath : p_filePath;
  ContentMediaUtils::copyMediaFiles(mediaSourceFile, nullptr, destFilePath);

  if (p_option.m_exportAttachments) {
    exportAttachments(p_attachmentFolderPath, mediaSourceFile, outputFolder, destFilePath);
  }

  Q_UNUSED(p_resourcePath);
  return destFilePath;
}

void Exporter::exportAttachments(const QString &p_attachmentFolderPath,
                                 const QString &p_srcFilePath, const QString &p_outputFolder,
                                 const QString &p_destFilePath) {
  if (p_attachmentFolderPath.isEmpty() || !QDir(p_attachmentFolderPath).exists()) {
    return;
  }

  auto relativePath =
      PathUtils::relativePath(PathUtils::parentDirPath(p_srcFilePath), p_attachmentFolderPath);
  if (relativePath.isEmpty() || relativePath == QStringLiteral(".")) {
    relativePath = QFileInfo(p_attachmentFolderPath).fileName();
  }

  auto destAttachmentFolderPath = QDir(p_outputFolder).filePath(relativePath);
  destAttachmentFolderPath = FileUtils::renameIfExistsCaseInsensitive(destAttachmentFolderPath);
  FileUtils::copyDir(p_attachmentFolderPath, destAttachmentFolderPath, false);

  Q_UNUSED(p_destFilePath);
}

QString Exporter::doExportPdfAllInOne(const ExportOption &p_option,
                                      const QVector<ExportFileInfo> &p_files,
                                      const QString &p_batchName) {
  const auto outputFolder = makeOutputFolder(p_option.m_outputDir, p_batchName);
  if (outputFolder.isEmpty()) {
    emit logRequested(tr("Failed to create output folder under (%1).").arg(p_option.m_outputDir));
    return QString();
  }

  QTemporaryDir tmpDir;
  if (!tmpDir.isValid()) {
    emit logRequested(tr("Failed to create temporary directory to hold HTML files."));
    return QString();
  }

  auto tmpOption(getExportOptionForIntermediateHtml(p_option, tmpDir.path()));

  QStringList htmlFiles;
  emit progressUpdated(0, p_files.size());
  for (int i = 0; i < p_files.size(); ++i) {
    if (checkAskedToStop()) {
      return QString();
    }

    const auto htmlFile =
        doExportHtml(tmpOption, tmpDir.path(), QString(), p_files[i].filePath, p_files[i].fileName,
                     p_files[i].resourcePath, QString(), QString());
    if (!htmlFile.isEmpty()) {
      htmlFiles << htmlFile;
    }
    emit progressUpdated(i + 1, p_files.size());
  }

  cleanUpWebViewExporter();

  if (htmlFiles.isEmpty()) {
    return QString();
  }

  if (checkAskedToStop()) {
    return QString();
  }

  auto fileName =
      FileUtils::generateFileNameWithSequence(outputFolder, tr("all_in_one_export"), "pdf");
  auto destFilePath = PathUtils::concatenateFilePath(outputFolder, fileName);
  if (getWebViewExporter(p_option)->htmlToPdfViaWkhtmltopdf(p_option.m_pdfOption, htmlFiles,
                                                            destFilePath)) {
    emit logRequested(tr("Exported to (%1).").arg(destFilePath));
    return destFilePath;
  }

  return QString();
}

QString Exporter::doExportCustomAllInOne(const ExportOption &p_option,
                                         const QVector<ExportFileInfo> &p_files,
                                         const QString &p_batchName) {
  const auto outputFolder = makeOutputFolder(p_option.m_outputDir, p_batchName);
  if (outputFolder.isEmpty()) {
    emit logRequested(tr("Failed to create output folder under (%1).").arg(p_option.m_outputDir));
    return QString();
  }

  QStringList inputFiles;
  QStringList resourcePaths;

  QTemporaryDir tmpDir;
  if (p_option.m_customOption && p_option.m_customOption->m_useHtmlInput) {
    if (!tmpDir.isValid()) {
      emit logRequested(tr("Failed to create temporary directory to hold HTML files."));
      return QString();
    }

    auto tmpOption(getExportOptionForIntermediateHtml(p_option, tmpDir.path()));

    emit progressUpdated(0, p_files.size());
    for (int i = 0; i < p_files.size(); ++i) {
      if (checkAskedToStop()) {
        return QString();
      }

      const auto htmlFile =
          doExportHtml(tmpOption, tmpDir.path(), QString(), p_files[i].filePath,
                       p_files[i].fileName, p_files[i].resourcePath, QString(), QString());
      if (!htmlFile.isEmpty()) {
        inputFiles << htmlFile;
        resourcePaths << PathUtils::parentDirPath(htmlFile);
      }
      emit progressUpdated(i + 1, p_files.size());
    }

    cleanUpWebViewExporter();

    if (inputFiles.isEmpty()) {
      return QString();
    }

    if (checkAskedToStop()) {
      return QString();
    }
  } else {
    emit progressUpdated(0, p_files.size());
    for (int i = 0; i < p_files.size(); ++i) {
      if (checkAskedToStop()) {
        return QString();
      }

      inputFiles << p_files[i].filePath;
      resourcePaths << p_files[i].resourcePath;
      emit progressUpdated(i + 1, p_files.size());
    }
  }

  if (inputFiles.isEmpty()) {
    return QString();
  }

  auto suffix = p_option.m_customOption ? p_option.m_customOption->m_targetSuffix : QString();
  auto fileName =
      FileUtils::generateFileNameWithSequence(outputFolder, tr("all_in_one_export"), suffix);
  auto destFilePath = PathUtils::concatenateFilePath(outputFolder, fileName);
  bool success = doExportCustom(p_option, inputFiles, resourcePaths, destFilePath);
  if (success) {
    emit logRequested(tr("Exported to (%1).").arg(destFilePath));
    return destFilePath;
  }

  return QString();
}

QString Exporter::doExportHtml(const ExportOption &p_option, const QString &p_outputDir,
                               const QString &p_content, const QString &p_filePath,
                               const QString &p_fileName, const QString &p_resourcePath,
                               const QString &p_destPath, const QString &p_attachmentFolderPath) {
  auto outputFilePath = p_destPath;
  if (outputFilePath.isEmpty()) {
    QString suffix =
        p_option.m_htmlOption.m_useMimeHtmlFormat ? QStringLiteral("mht") : QStringLiteral("html");
    auto baseName = QFileInfo(p_fileName).completeBaseName();
    if (baseName.isEmpty()) {
      baseName = QFileInfo(p_filePath).completeBaseName();
    }
    auto fileName = FileUtils::generateFileNameWithSequence(p_outputDir, baseName, suffix);
    outputFilePath = PathUtils::concatenateFilePath(p_outputDir, fileName);
  }

  bool success = getWebViewExporter(p_option)->doExport(p_option, p_content, p_filePath, p_fileName,
                                                        p_resourcePath, outputFilePath);
  if (success) {
    if (p_option.m_exportAttachments) {
      exportAttachments(p_attachmentFolderPath, p_filePath, p_outputDir, outputFilePath);
    }
    return outputFilePath;
  }

  return QString();
}

WebViewExporter *Exporter::getWebViewExporter(const ExportOption &p_option) {
  if (!m_webViewExporter) {
    m_webViewExporter = new WebViewExporter(m_services, static_cast<QWidget *>(parent()));
    connect(m_webViewExporter, &WebViewExporter::logRequested, this, &Exporter::logRequested);
    m_webViewExporter->prepare(p_option);
  }

  return m_webViewExporter;
}

void Exporter::cleanUpWebViewExporter() {
  if (m_webViewExporter) {
    m_webViewExporter->clear();
    delete m_webViewExporter;
    m_webViewExporter = nullptr;
  }
}

void Exporter::cleanUp() { cleanUpWebViewExporter(); }

void Exporter::stop() {
  m_askedToStop = true;
  emit askedToStop();

  if (m_webViewExporter) {
    m_webViewExporter->stop();
  }
}

bool Exporter::checkAskedToStop() const {
  if (m_askedToStop) {
    emit const_cast<Exporter *>(this)->logRequested(tr("Asked to stop. Aborting."));
    return true;
  }

  return false;
}

QString Exporter::doExportPdf(const ExportOption &p_option, const QString &p_outputDir,
                              const QString &p_content, const QString &p_filePath,
                              const QString &p_fileName, const QString &p_resourcePath,
                              const QString &p_destPath, const QString &p_attachmentFolderPath) {
  auto outputFilePath = p_destPath;
  if (outputFilePath.isEmpty()) {
    auto baseName = QFileInfo(p_fileName).completeBaseName();
    if (baseName.isEmpty()) {
      baseName = QFileInfo(p_filePath).completeBaseName();
    }
    auto fileName = FileUtils::generateFileNameWithSequence(p_outputDir, baseName, "pdf");
    outputFilePath = PathUtils::concatenateFilePath(p_outputDir, fileName);
  }

  bool success = getWebViewExporter(p_option)->doExport(p_option, p_content, p_filePath, p_fileName,
                                                        p_resourcePath, outputFilePath);
  if (success) {
    if (p_option.m_exportAttachments) {
      exportAttachments(p_attachmentFolderPath, p_filePath, p_outputDir, outputFilePath);
    }
    return outputFilePath;
  }

  return QString();
}

QString Exporter::doExportCustom(const ExportOption &p_option, const QString &p_outputDir,
                                 const QString &p_content, const QString &p_filePath,
                                 const QString &p_fileName, const QString &p_resourcePath,
                                 const QString &p_destPath, const QString &p_attachmentFolderPath) {
  Q_ASSERT(p_option.m_customOption);

  QStringList inputFiles;
  QStringList resourcePaths;

  QTemporaryDir tmpDir;
  if (p_option.m_customOption->m_useHtmlInput) {
    if (!tmpDir.isValid()) {
      emit logRequested(tr("Failed to create temporary directory to hold HTML files."));
      return QString();
    }

    auto tmpOption(getExportOptionForIntermediateHtml(p_option, tmpDir.path()));
    auto htmlFile = doExportHtml(tmpOption, tmpDir.path(), p_content, p_filePath, p_fileName,
                                 p_resourcePath, QString(), QString());
    if (htmlFile.isEmpty()) {
      return QString();
    }

    if (checkAskedToStop()) {
      return QString();
    }

    cleanUpWebViewExporter();

    inputFiles << htmlFile;
    resourcePaths << PathUtils::parentDirPath(htmlFile);
  } else {
    inputFiles << p_filePath;
    resourcePaths << p_resourcePath;
  }

  auto outputFilePath = p_destPath;
  if (outputFilePath.isEmpty()) {
    auto baseName = QFileInfo(p_fileName).completeBaseName();
    if (baseName.isEmpty()) {
      baseName = QFileInfo(p_filePath).completeBaseName();
    }
    auto fileName = FileUtils::generateFileNameWithSequence(
        p_outputDir, baseName, p_option.m_customOption->m_targetSuffix);
    outputFilePath = PathUtils::concatenateFilePath(p_outputDir, fileName);
  }

  bool success = doExportCustom(p_option, inputFiles, resourcePaths, outputFilePath);
  if (success) {
    if (p_option.m_exportAttachments) {
      exportAttachments(p_attachmentFolderPath, p_filePath, p_outputDir, outputFilePath);
    }

    return outputFilePath;
  }

  return QString();
}

ExportOption Exporter::getExportOptionForIntermediateHtml(const ExportOption &p_option,
                                                          const QString &p_outputDir) {
  ExportOption tmpOption(p_option);
  tmpOption.m_exportAttachments = false;
  tmpOption.m_targetFormat = ExportFormat::HTML;
  tmpOption.m_transformSvgToPngEnabled = true;
  tmpOption.m_removeCodeToolBarEnabled = true;

  tmpOption.m_htmlOption.m_embedStyles = true;
  tmpOption.m_htmlOption.m_completePage = true;
  tmpOption.m_htmlOption.m_embedImages = false;
  tmpOption.m_htmlOption.m_useMimeHtmlFormat = false;
  tmpOption.m_htmlOption.m_addOutlinePanel = false;
  tmpOption.m_htmlOption.m_scrollable = false;
  if (p_option.m_targetFormat == ExportFormat::Custom && p_option.m_customOption &&
      p_option.m_customOption->m_targetPageScrollable) {
    tmpOption.m_htmlOption.m_scrollable = true;
  }
  tmpOption.m_outputDir = p_outputDir;
  return tmpOption;
}

bool Exporter::doExportCustom(const ExportOption &p_option, const QStringList &p_files,
                              const QStringList &p_resourcePaths, const QString &p_filePath) {
  const auto cmd = evaluateCommand(p_option, p_files, p_resourcePaths, p_filePath);

  emit logRequested(tr("Custom command: %1").arg(cmd));
  qDebug() << "custom export" << cmd;

  auto state = ProcessUtils::start(
      cmd, [this](const QString &msg) { emit logRequested(msg); }, m_askedToStop);

  return state == ProcessUtils::Succeeded;
}

QString Exporter::evaluateCommand(const ExportOption &p_option, const QStringList &p_files,
                                  const QStringList &p_resourcePaths, const QString &p_filePath) {
  auto cmd(p_option.m_customOption->m_command);

  QString inputs;
  for (int i = 0; i < p_files.size(); ++i) {
    if (i > 0) {
      inputs += " ";
    }

    inputs += getQuotedPath(p_files[i]);
  }

  QString resourcePath;
  for (int i = 0; i < p_resourcePaths.size(); ++i) {
    bool duplicated = false;
    for (int j = 0; j < i; ++j) {
      if (p_resourcePaths[j] == p_resourcePaths[i]) {
        duplicated = true;
        break;
      }
    }

    if (duplicated) {
      continue;
    }

    if (!resourcePath.isEmpty()) {
      resourcePath += p_option.m_customOption->m_resourcePathSeparator;
    }

    resourcePath += getQuotedPath(p_resourcePaths[i]);
  }

  cmd.replace("%1", inputs);
  cmd.replace("%2", resourcePath);
  cmd.replace("%3", getQuotedPath(p_option.m_renderingStyleFile));
  cmd.replace("%4", getQuotedPath(p_option.m_syntaxHighlightStyleFile));
  cmd.replace("%5", getQuotedPath(p_filePath));
  return cmd;
}

QString Exporter::getQuotedPath(const QString &p_path) {
  return QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(p_path));
}
