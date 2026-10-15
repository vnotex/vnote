#include "exporter.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QWidget>
#include <utility>

#include "htmlpdfexporter.h"
#include <utils/contentmediautils.h>
#include <utils/fileutils2.h>
#include <utils/pathutils.h>
#include <utils/processutils.h>

using namespace vnotex;

Exporter::Exporter(ServiceLocator &p_services, ExportRendererFactory p_renderers, QWidget *p_parent)
    : QObject(p_parent), m_services(p_services), m_renderers(std::move(p_renderers)) {}

static QString makeOutputFolder(const QString &p_outputDir, const QString &p_folderName) {
  const auto name = FileUtils2::generateFileNameWithSequence(p_outputDir, p_folderName);
  const auto outputFolder = PathUtils::concatenateFilePath(p_outputDir, name);
  if (!QDir().mkpath(outputFolder)) {
    return QString();
  }

  return outputFolder;
}

bool Exporter::loadInput(const ExportFileInfo &p_file, const QString *p_suppliedContent,
                         ExportInput &p_input) {
  p_input.file = p_file;
  if (p_suppliedContent) {
    p_input.content = *p_suppliedContent;
    return true;
  }
  const auto error = FileUtils2::readTextFile(p_file.filePath, &p_input.content);
  if (error) {
    emit logRequested(tr("Failed to read file (%1): %2").arg(p_file.filePath, error.what()));
    return false;
  }
  return true;
}

QString Exporter::doExportFile(const ExportOption &p_option, const QString &p_content,
                               const QString &p_filePath, const QString &p_fileName,
                               const QString &p_resourcePath, const QString &p_attachmentFolderPath,
                               const QString &p_fileTypeName) {
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
  fileInfo.fileTypeName = p_fileTypeName;

  ExportInput input;
  QString outputFile;
  if (loadInput(fileInfo, &p_content, input)) {
    outputFile = doExport(p_option, p_option.m_outputDir, input);
  }
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

      ExportInput input;
      if (loadInput(p_files[i], nullptr, input)) {
        const auto outputFile = doExport(p_option, outputFolder, input);
        if (!outputFile.isEmpty()) {
          outputFiles << outputFile;
        }
      }

      emit progressUpdated(i + 1, p_files.size());
    }
  }

  cleanUp();
  return outputFiles;
}

QString Exporter::doExport(const ExportOption &p_option, const QString &p_outputDir,
                           const ExportInput &p_input) {
  QString output;
  switch (p_option.m_targetFormat) {
  case ExportFormat::Markdown:
    output = doExportMarkdown(p_outputDir, p_input);
    break;
  case ExportFormat::HTML:
    output = doExportHtml(getExportOptionForIntermediateHtml(p_option, p_outputDir), p_outputDir,
                          p_input);
    break;
  case ExportFormat::PDF:
    output = doExportPdf(p_option, p_outputDir, p_input);
    break;
  case ExportFormat::Custom:
    output = doExportCustom(p_option, p_outputDir, p_input);
    break;
  default:
    emit logRequested(
        tr("Unknown target format %1.").arg(exportFormatString(p_option.m_targetFormat)));
    break;
  }
  const auto &file = p_input.file;
  const auto source = file.filePath.isEmpty() ? file.fileName : file.filePath;
  if (!output.isEmpty()) {
    if (p_option.m_exportAttachments) {
      exportAttachments(file.attachmentFolderPath, file.filePath, QFileInfo(output).absolutePath(),
                        output);
    }
    emit logRequested(tr("File (%1) exported to (%2)").arg(source, output));
  } else {
    emit logRequested(tr("Failed to export file (%1)").arg(source));
  }
  return output;
}

QString Exporter::doExportMarkdown(const QString &p_outputDir, const ExportInput &p_input) {
  auto *adapter = renderer(p_input.file.fileTypeName);
  ExportMarkdown markdown;
  if (!adapter || !adapter->renderMarkdown(p_input, markdown) || checkAskedToStop()) {
    return QString();
  }
  const auto name = FileUtils2::generateFileNameWithSequence(p_outputDir, markdown.fileName, "");
  const auto folder = QDir(p_outputDir).filePath(name);
  if (!QDir().mkpath(folder)) {
    emit logRequested(tr("Failed to create output folder under (%1).").arg(p_outputDir));
    return QString();
  }
  const auto destination = QDir(folder).filePath(markdown.fileName);
  const auto error = FileUtils2::writeFile(destination, markdown.content);
  if (error) {
    emit logRequested(tr("Failed to write file (%1): %2").arg(destination, error.what()));
    QFile::remove(destination);
    return QString();
  }
  if (markdown.copyMedia) {
    ContentMediaUtils::copyMarkdownMediaFiles(markdown.content, p_input.file.resourcePath, nullptr,
                                              destination);
  }
  return destination;
}

QString Exporter::outputPath(const ExportInput &p_input, const QString &p_outputDir,
                             const QString &p_suffix) {
  auto base = QFileInfo(p_input.file.fileName).completeBaseName();
  if (base.isEmpty()) {
    base = QFileInfo(p_input.file.filePath).completeBaseName();
  }
  return QDir(p_outputDir)
      .filePath(FileUtils2::generateFileNameWithSequence(p_outputDir, base, p_suffix));
}

QString Exporter::doExportHtml(const ExportHtmlRequest &p_request, const QString &p_outputDir,
                               const ExportInput &p_input) {
  auto *adapter = renderer(p_input.file.fileTypeName);
  if (!adapter || checkAskedToStop()) {
    return QString();
  }
  const auto destination =
      outputPath(p_input, p_outputDir,
                 p_request.options.m_htmlOption.m_useMimeHtmlFormat ? QStringLiteral("mht")
                                                                    : QStringLiteral("html"));
  if (!adapter->renderHtml(p_input, p_request, destination) || checkAskedToStop()) {
    QFile::remove(destination);
    return QString();
  }
  return destination;
}

QString Exporter::doExportPdf(const ExportOption &p_option, const QString &p_outputDir,
                              const ExportInput &p_input) {
  QTemporaryDir temporary;
  if (!temporary.isValid()) {
    emit logRequested(tr("Failed to create temporary directory to hold HTML files."));
    return QString();
  }
  const auto html = doExportHtml(getExportOptionForIntermediateHtml(p_option, temporary.path()),
                                 temporary.path(), p_input);
  if (html.isEmpty() || checkAskedToStop()) {
    return QString();
  }
  const auto destination = outputPath(p_input, p_outputDir, QStringLiteral("pdf"));
  return pdfExporter()->exportFiles(p_option.m_pdfOption, {html}, destination) ? destination
                                                                               : QString();
}

QStringList Exporter::renderBatchHtml(const ExportHtmlRequest &p_request,
                                      const QVector<ExportFileInfo> &p_files,
                                      const QString &p_outputDir) {
  QStringList htmlFiles;
  emit progressUpdated(0, p_files.size());
  for (int i = 0; i < p_files.size(); ++i) {
    if (checkAskedToStop()) {
      return {};
    }
    ExportInput input;
    if (loadInput(p_files[i], nullptr, input)) {
      const auto html = doExportHtml(p_request, p_outputDir, input);
      if (!html.isEmpty()) {
        htmlFiles << html;
      } else {
        emit logRequested(tr("Failed to export file (%1)").arg(p_files[i].filePath));
      }
    }
    emit progressUpdated(i + 1, p_files.size());
  }
  return htmlFiles;
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

  const auto htmlFiles = renderBatchHtml(
      getExportOptionForIntermediateHtml(p_option, tmpDir.path()), p_files, tmpDir.path());

  if (htmlFiles.isEmpty()) {
    return QString();
  }

  if (checkAskedToStop()) {
    return QString();
  }

  auto fileName =
      FileUtils2::generateFileNameWithSequence(outputFolder, tr("all_in_one_export"), "pdf");
  auto destFilePath = PathUtils::concatenateFilePath(outputFolder, fileName);
  if (pdfExporter()->exportFiles(p_option.m_pdfOption, htmlFiles, destFilePath)) {
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

    inputFiles = renderBatchHtml(getExportOptionForIntermediateHtml(p_option, tmpDir.path()),
                                 p_files, tmpDir.path());
    for (const auto &input : inputFiles) {
      resourcePaths << QFileInfo(input).absolutePath();
    }

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
      FileUtils2::generateFileNameWithSequence(outputFolder, tr("all_in_one_export"), suffix);
  auto destFilePath = PathUtils::concatenateFilePath(outputFolder, fileName);
  bool success = doExportCustom(p_option, inputFiles, resourcePaths, destFilePath);
  if (success) {
    emit logRequested(tr("Exported to (%1).").arg(destFilePath));
    return destFilePath;
  }

  return QString();
}

QString Exporter::doExportCustom(const ExportOption &p_option, const QString &p_outputDir,
                                 const ExportInput &p_input) {
  Q_ASSERT(p_option.m_customOption);
  QTemporaryDir temporary;
  QString input = p_input.file.filePath;
  QString resourcePath = p_input.file.resourcePath;
  if (p_option.m_customOption->m_useHtmlInput) {
    if (!temporary.isValid()) {
      emit logRequested(tr("Failed to create temporary directory to hold HTML files."));
      return QString();
    }
    input = doExportHtml(getExportOptionForIntermediateHtml(p_option, temporary.path()),
                         temporary.path(), p_input);
    resourcePath = QFileInfo(input).absolutePath();
  }
  if (input.isEmpty() || checkAskedToStop()) {
    return QString();
  }
  const auto destination =
      outputPath(p_input, p_outputDir, p_option.m_customOption->m_targetSuffix);
  return doExportCustom(p_option, {input}, {resourcePath}, destination) ? destination : QString();
}

ExportRenderer *Exporter::renderer(const QString &p_fileTypeName) {
  const auto key = p_fileTypeName.toLower();
  auto *adapter = m_activeRenderers.value(key);
  if (!adapter) {
    adapter = m_renderers.create(p_fileTypeName, qobject_cast<QWidget *>(parent()), this);
    if (!adapter) {
      emit logRequested(tr("No export renderer registered for (%1).").arg(p_fileTypeName));
      return nullptr;
    }
    connect(adapter, &ExportRenderer::logRequested, this, &Exporter::logRequested);
    m_activeRenderers.insert(key, adapter);
  }
  return adapter;
}

HtmlPdfExporter *Exporter::pdfExporter() {
  if (!m_pdfExporter) {
    m_pdfExporter = new HtmlPdfExporter(m_services, this);
    connect(m_pdfExporter, &HtmlPdfExporter::logRequested, this, &Exporter::logRequested);
  }
  return m_pdfExporter;
}

void Exporter::cleanUp() {
  qDeleteAll(m_activeRenderers);
  m_activeRenderers.clear();
  delete m_pdfExporter;
  m_pdfExporter = nullptr;
}

void Exporter::stop() {
  m_askedToStop = true;
  emit askedToStop();
  for (auto *adapter : m_activeRenderers) {
    adapter->stop();
  }
  if (m_pdfExporter) {
    m_pdfExporter->stop();
  }
}

bool Exporter::checkAskedToStop() const {
  if (m_askedToStop) {
    emit const_cast<Exporter *>(this)->logRequested(tr("Asked to stop. Aborting."));
  }
  return m_askedToStop;
}

ExportHtmlRequest Exporter::getExportOptionForIntermediateHtml(const ExportOption &p_option,
                                                               const QString &p_outputDir) {
  ExportHtmlRequest request;
  request.options = p_option;
  if (p_option.m_targetFormat == ExportFormat::HTML) {
    return request;
  }
  const bool pdf = p_option.m_targetFormat == ExportFormat::PDF;
  request.purpose = pdf ? ExportHtmlPurpose::Pdf : ExportHtmlPurpose::Custom;
  auto &options = request.options;
  options.m_exportAttachments = false;
  options.m_targetFormat = ExportFormat::HTML;
  options.m_intermediateHtml = true;
  options.m_transformSvgToPngEnabled = !pdf || p_option.m_pdfOption.m_useWkhtmltopdf;
  options.m_rasterizeDiagramsEnabled = pdf && p_option.m_pdfOption.m_useWkhtmltopdf;
  options.m_rasterizeMathEnabled = true;
  options.m_removeCodeToolBarEnabled = true;
  options.m_htmlOption.m_embedStyles = true;
  options.m_htmlOption.m_completePage = true;
  options.m_htmlOption.m_embedImages = false;
  options.m_htmlOption.m_useMimeHtmlFormat = false;
  options.m_htmlOption.m_addOutlinePanel = false;
  options.m_htmlOption.m_scrollable =
      !pdf && p_option.m_customOption && p_option.m_customOption->m_targetPageScrollable;
  options.m_outputDir = p_outputDir;
  return request;
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
  destAttachmentFolderPath = FileUtils2::renameIfExistsCaseInsensitive(destAttachmentFolderPath);
  Error err = FileUtils2::copyDir(p_attachmentFolderPath, destAttachmentFolderPath, false);
  if (err) {
    emit logRequested(
        tr("Failed to copy attachment folder (%1): %2").arg(p_attachmentFolderPath, err.what()));
  }

  Q_UNUSED(p_destFilePath);
}

bool Exporter::doExportCustom(const ExportOption &p_option, const QStringList &p_files,
                              const QStringList &p_resourcePaths, const QString &p_filePath) {
  const auto cmd = evaluateCommand(p_option, p_files, p_resourcePaths, p_filePath);

  emit logRequested(tr("Custom command: %1").arg(cmd));
  qDebug() << "custom export" << cmd;

  auto state = ProcessUtils::start(
      cmd, [this](const QString &msg) { emit logRequested(msg); }, m_askedToStop);

  if (state != ProcessUtils::Succeeded || checkAskedToStop() || !QFileInfo(p_filePath).isFile()) {
    QFile::remove(p_filePath);
    return false;
  }
  return true;
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
