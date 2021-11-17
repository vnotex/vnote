#include "exporter.h"

#include <QWidget>
#include <QTemporaryDir>

#include <notebook/notebook.h>
#include <notebook/node.h>
#include <buffer/buffer.h>
#include <core/file.h>
#include <utils/fileutils.h>
#include <utils/utils.h>
#include <utils/pathutils.h>
#include <utils/processutils.h>
#include <utils/contentmediautils.h>
#include "webviewexporter.h"
#include <core/exception.h>

using namespace vnotex;

Exporter::Exporter(QWidget *p_parent)
    : QObject(p_parent)
{
}

QString Exporter::doExport(const ExportOption &p_option, Buffer *p_buffer)
{
    m_askedToStop = false;

    QString outputFile;
    auto file = p_buffer->getFile();
    if (!file) {
        emit logRequested(tr("Skipped buffer (%1) without file base.").arg(p_buffer->getName()));
        return outputFile;
    }

    // Make sure output folder exists.
    if (!QDir().mkpath(p_option.m_outputDir)) {
        emit logRequested(tr("Failed to create output folder (%1).").arg(p_option.m_outputDir));
        return outputFile;
    }

    outputFile = doExport(p_option, p_option.m_outputDir, file.data());

    cleanUp();

    return outputFile;
}

static QString makeOutputFolder(const QString &p_outputDir, const QString &p_folderName)
{
    const auto name = FileUtils::generateFileNameWithSequence(p_outputDir, p_folderName);
    const auto outputFolder = PathUtils::concatenateFilePath(p_outputDir, name);
    if (!QDir().mkpath(outputFolder)) {
        return QString();
    }

    return outputFolder;
}

QString Exporter::doExportMarkdown(const ExportOption &p_option, const QString &p_outputDir, const File *p_file)
{
    QString outputFile;
    if (!p_file->getContentType().isMarkdown()) {
        emit logRequested(tr("Format %1 is not supported to export as Markdown.").arg(p_file->getContentType().m_displayName));
        return outputFile;
    }

    // Export it to a folder with the same name.
    const auto name = FileUtils::generateFileNameWithSequence(p_outputDir, p_file->getName(), "");
    const auto outputFolder = PathUtils::concatenateFilePath(p_outputDir, name);
    QDir outDir(outputFolder);
    if (!outDir.mkpath(outputFolder)) {
        emit logRequested(tr("Failed to create output folder under (%1).").arg(p_outputDir));
        return outputFile;
    }

    // Copy source file itself.
    const auto srcFilePath = p_file->getFilePath();
    auto destFilePath = outDir.filePath(p_file->getName());
    FileUtils::copyFile(srcFilePath, destFilePath, false);
    outputFile = destFilePath;

    ContentMediaUtils::copyMediaFiles(p_file, destFilePath);

    // Copy attachments if available.
    if (p_option.m_exportAttachments) {
        exportAttachments(p_file->getNode(), srcFilePath, outputFolder, destFilePath);
    }

    return outputFile;
}

void Exporter::exportAttachments(Node *p_node,
                                 const QString &p_srcFilePath,
                                 const QString &p_outputFolder,
                                 const QString &p_destFilePath)
{
    if (!p_node) {
        return;
    }
    const auto &attachmentFolder = p_node->getAttachmentFolder();
    if (!attachmentFolder.isEmpty()) {
        auto relativePath = PathUtils::relativePath(PathUtils::parentDirPath(p_srcFilePath),
                                                    p_node->fetchAttachmentFolderPath());
        auto destAttachmentFolderPath = QDir(p_outputFolder).filePath(relativePath);
        destAttachmentFolderPath = FileUtils::renameIfExistsCaseInsensitive(destAttachmentFolderPath);
        ContentMediaUtils::copyAttachment(p_node, nullptr, p_destFilePath, destAttachmentFolderPath);
    }
}

QString Exporter::doExport(const ExportOption &p_option, Node *p_note)
{
    m_askedToStop = false;

    QString outputFile;
    auto file = p_note->getContentFile();

    // Make sure output folder exists.
    if (!QDir().mkpath(p_option.m_outputDir)) {
        emit logRequested(tr("Failed to create output folder (%1).").arg(p_option.m_outputDir));
        return outputFile;
    }

    outputFile = doExport(p_option, p_option.m_outputDir, file.data());

    cleanUp();

    return outputFile;
}

QString Exporter::doExportPdfAllInOne(const ExportOption &p_option, Notebook *p_notebook, Node *p_folder)
{
    Q_ASSERT((p_notebook || p_folder) && !(p_notebook && p_folder));

    // Make path.
    const auto name = p_notebook ? tr("notebook_%1").arg(p_notebook->getName()) : p_folder->getName();
    const auto outputFolder = makeOutputFolder(p_option.m_outputDir, name);
    if (outputFolder.isEmpty()) {
        emit logRequested(tr("Failed to create output folder under (%1).").arg(p_option.m_outputDir));
        return QString();
    }

    // Export to HTML to a tmp dir first.
    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        emit logRequested(tr("Failed to create temporary directory to hold HTML files."));
        return QString();
    }

    auto tmpOption(getExportOptionForIntermediateHtml(p_option, tmpDir.path()));

    QStringList htmlFiles;
    if (p_notebook) {
        htmlFiles = doExportNotebook(tmpOption, tmpDir.path(), p_notebook);
    } else {
        htmlFiles = doExport(tmpOption, tmpDir.path(), p_folder);
    }

    cleanUpWebViewExporter();

    if (htmlFiles.isEmpty()) {
        return QString();
    }

    if (checkAskedToStop()) {
        return QString();
    }

    auto fileName = FileUtils::generateFileNameWithSequence(outputFolder,
                                                            tr("all_in_one_export"),
                                                            "pdf");
    auto destFilePath = PathUtils::concatenateFilePath(outputFolder, fileName);
    if (getWebViewExporter(p_option)->htmlToPdfViaWkhtmltopdf(p_option.m_pdfOption, htmlFiles, destFilePath)) {
        emit logRequested(tr("Exported to (%1).").arg(destFilePath));
        return destFilePath;
    }

    return QString();
}

QString Exporter::doExportCustomAllInOne(const ExportOption &p_option, Notebook *p_notebook, Node *p_folder)
{
    Q_ASSERT((p_notebook || p_folder) && !(p_notebook && p_folder));

    // Make path.
    const auto name = p_notebook ? tr("notebook_%1").arg(p_notebook->getName()) : p_folder->getName();
    const auto outputFolder = makeOutputFolder(p_option.m_outputDir, name);
    if (outputFolder.isEmpty()) {
        emit logRequested(tr("Failed to create output folder under (%1).").arg(p_option.m_outputDir));
        return QString();
    }

    QStringList inputFiles;
    QStringList resourcePaths;

    QTemporaryDir tmpDir;
    if (p_option.m_customOption->m_useHtmlInput) {
        // Export to HTML to a tmp dir first.
        if (!tmpDir.isValid()) {
            emit logRequested(tr("Failed to create temporary directory to hold HTML files."));
            return QString();
        }

        auto tmpOption(getExportOptionForIntermediateHtml(p_option, tmpDir.path()));

        QStringList htmlFiles;
        if (p_notebook) {
            htmlFiles = doExportNotebook(tmpOption, tmpDir.path(), p_notebook);
        } else {
            htmlFiles = doExport(tmpOption, tmpDir.path(), p_folder);
        }

        cleanUpWebViewExporter();

        if (htmlFiles.isEmpty()) {
            return QString();
        }

        if (checkAskedToStop()) {
            return QString();
        }

        inputFiles = htmlFiles;
        for (const auto &file : htmlFiles) {
            resourcePaths << PathUtils::parentDirPath(file);
        }
    } else {
        // Collect source files.
        if (p_notebook) {
            collectFiles(p_notebook->collectFiles(), inputFiles, resourcePaths);
        } else {
            collectFiles(p_folder->collectFiles(), inputFiles, resourcePaths);
        }

        if (checkAskedToStop()) {
            return QString();
        }
    }

    if (inputFiles.isEmpty()) {
        return QString();
    }

    auto fileName = FileUtils::generateFileNameWithSequence(outputFolder,
                                                            tr("all_in_one_export"),
                                                            p_option.m_customOption->m_targetSuffix);
    auto destFilePath = PathUtils::concatenateFilePath(outputFolder, fileName);
    bool success = doExportCustom(p_option,
                                  inputFiles,
                                  resourcePaths,
                                  destFilePath);
    if (success) {
        emit logRequested(tr("Exported to (%1).").arg(destFilePath));
        return destFilePath;
    }

    return QString();
}

QStringList Exporter::doExportFolder(const ExportOption &p_option, Node *p_folder)
{
    m_askedToStop = false;

    QStringList outputFiles;

    if (p_option.m_targetFormat == ExportFormat::PDF
        && p_option.m_pdfOption.m_useWkhtmltopdf
        && p_option.m_pdfOption.m_allInOne) {
        auto file = doExportPdfAllInOne(p_option, nullptr, p_folder);
        if (!file.isEmpty()) {
            outputFiles << file;
        }
    } else if (p_option.m_targetFormat == ExportFormat::Custom
               && p_option.m_customOption->m_allInOne) {
        auto file = doExportCustomAllInOne(p_option, nullptr, p_folder);
        if (!file.isEmpty()) {
            outputFiles << file;
        }
    } else {
        outputFiles = doExport(p_option, p_option.m_outputDir, p_folder);
    }

    cleanUp();

    return outputFiles;
}

QStringList Exporter::doExport(const ExportOption &p_option, const QString &p_outputDir, Node *p_folder)
{
    Q_ASSERT(p_folder->isContainer());

    QStringList outputFiles;

    // Make path.
    const auto outputFolder = makeOutputFolder(p_outputDir, p_folder->getName());
    if (outputFolder.isEmpty()) {
        emit logRequested(tr("Failed to create output folder under (%1).").arg(p_outputDir));
        return outputFiles;
    }

    try {
        p_folder->load();
    } catch (Exception &p_e) {
        QString msg = tr("Failed to load node (%1) (%2).").arg(p_folder->fetchPath(), p_e.what());
        qWarning() << msg;
        emit logRequested(msg);
        return outputFiles;
    }

    const auto &children = p_folder->getChildrenRef();
    emit progressUpdated(0, children.size());
    for (int i = 0; i < children.size(); ++i) {
        if (checkAskedToStop()) {
            break;
        }

        const auto &child = children[i];
        if (child->hasContent()) {
            auto outputFile = doExport(p_option, outputFolder, child->getContentFile().data());
            if (!outputFile.isEmpty()) {
                outputFiles << outputFile;
            }
        }
        if (p_option.m_recursive && child->isContainer() && child->getUse() == Node::Use::Normal) {
            outputFiles.append(doExport(p_option, outputFolder, child.data()));
        }

        emit progressUpdated(i + 1, children.size());
    }

    return outputFiles;
}

QString Exporter::doExport(const ExportOption &p_option, const QString &p_outputDir, const File *p_file)
{
    QString outputFile;

    switch (p_option.m_targetFormat) {
    case ExportFormat::Markdown:
        outputFile = doExportMarkdown(p_option, p_outputDir, p_file);
        break;

    case ExportFormat::HTML:
        outputFile = doExportHtml(p_option, p_outputDir, p_file);
        break;

    case ExportFormat::PDF:
        outputFile = doExportPdf(p_option, p_outputDir, p_file);
        break;

    case ExportFormat::Custom:
        outputFile = doExportCustom(p_option, p_outputDir, p_file);
        break;

    default:
        emit logRequested(tr("Unknown target format %1.").arg(exportFormatString(p_option.m_targetFormat)));
        break;
    }

    if (!outputFile.isEmpty()) {
        emit logRequested(tr("File (%1) exported to (%2)").arg(p_file->getFilePath(), outputFile));
    } else {
        emit logRequested(tr("Failed to export file (%1)").arg(p_file->getFilePath()));
    }

    return outputFile;
}

QStringList Exporter::doExport(const ExportOption &p_option, Notebook *p_notebook)
{
    m_askedToStop = false;

    QStringList outputFiles;

    if (p_option.m_targetFormat == ExportFormat::PDF
        && p_option.m_pdfOption.m_useWkhtmltopdf
        && p_option.m_pdfOption.m_allInOne) {
        auto file = doExportPdfAllInOne(p_option, p_notebook, nullptr);
        if (!file.isEmpty()) {
            outputFiles << file;
        }
    } else if (p_option.m_targetFormat == ExportFormat::Custom
               && p_option.m_customOption->m_allInOne) {
        auto file = doExportCustomAllInOne(p_option, p_notebook, nullptr);
        if (!file.isEmpty()) {
            outputFiles << file;
        }
    } else {
        outputFiles = doExportNotebook(p_option, p_option.m_outputDir, p_notebook);
    }

    cleanUp();

    return outputFiles;
}

QStringList Exporter::doExportNotebook(const ExportOption &p_option, const QString &p_outputDir, Notebook *p_notebook)
{
    m_askedToStop = false;

    QStringList outputFiles;

    // Make path.
    const auto outputFolder = makeOutputFolder(p_outputDir, tr("notebook_%1").arg(p_notebook->getName()));
    if (outputFolder.isEmpty()) {
        emit logRequested(tr("Failed to create output folder under (%1).").arg(p_outputDir));
        return outputFiles;
    }

    auto rootNode = p_notebook->getRootNode();
    Q_ASSERT(rootNode->isLoaded());

    const auto &children = rootNode->getChildrenRef();
    emit progressUpdated(0, children.size());
    for (int i = 0; i < children.size(); ++i) {
        if (checkAskedToStop()) {
            break;
        }

        const auto &child = children[i];
        if (child->hasContent()) {
            auto outputFile = doExport(p_option, outputFolder, child->getContentFile().data());
            if (!outputFile.isEmpty()) {
                outputFiles << outputFile;
            }
        }
        if (child->isContainer() && child->getUse() == Node::Use::Normal) {
            outputFiles.append(doExport(p_option, outputFolder, child.data()));
        }

        emit progressUpdated(i + 1, children.size());
    }

    cleanUp();

    return outputFiles;
}

QString Exporter::doExportHtml(const ExportOption &p_option, const QString &p_outputDir, const File *p_file)
{
    QString outputFile;
    if (!p_file->getContentType().isMarkdown()) {
        emit logRequested(tr("Format %1 is not supported to export as HTML.").arg(p_file->getContentType().m_displayName));
        return outputFile;
    }

    QString suffix = p_option.m_htmlOption.m_useMimeHtmlFormat ? QStringLiteral("mht") : QStringLiteral("html");
    auto fileName = FileUtils::generateFileNameWithSequence(p_outputDir,
                                                            QFileInfo(p_file->getName()).completeBaseName(),
                                                            suffix);
    auto destFilePath = PathUtils::concatenateFilePath(p_outputDir, fileName);

    bool success = getWebViewExporter(p_option)->doExport(p_option, p_file, destFilePath);
    if (success) {
        outputFile = destFilePath;

        // Copy attachments if available.
        if (p_option.m_exportAttachments) {
            exportAttachments(p_file->getNode(), p_file->getFilePath(), p_outputDir, destFilePath);
        }
    }
    return outputFile;
}

WebViewExporter *Exporter::getWebViewExporter(const ExportOption &p_option)
{
    if (!m_webViewExporter) {
        m_webViewExporter = new WebViewExporter(static_cast<QWidget *>(parent()));
        connect(m_webViewExporter, &WebViewExporter::logRequested,
                this, &Exporter::logRequested);
        m_webViewExporter->prepare(p_option);
    }

    return m_webViewExporter;
}

void Exporter::cleanUpWebViewExporter()
{
    if (m_webViewExporter) {
        m_webViewExporter->clear();
        delete m_webViewExporter;
        m_webViewExporter = nullptr;
    }
}

void Exporter::cleanUp()
{
    cleanUpWebViewExporter();
}

void Exporter::stop()
{
    m_askedToStop = true;

    if (m_webViewExporter) {
        m_webViewExporter->stop();
    }
}

bool Exporter::checkAskedToStop() const
{
    if (m_askedToStop) {
        emit const_cast<Exporter *>(this)->logRequested(tr("Asked to stop. Aborting."));
        return true;
    }

    return false;
}

QString Exporter::doExportPdf(const ExportOption &p_option, const QString &p_outputDir, const File *p_file)
{
    QString outputFile;
    if (!p_file->getContentType().isMarkdown()) {
        emit logRequested(tr("Format %1 is not supported to export as PDF.").arg(p_file->getContentType().m_displayName));
        return outputFile;
    }

    auto fileName = FileUtils::generateFileNameWithSequence(p_outputDir,
                                                            QFileInfo(p_file->getName()).completeBaseName(),
                                                            "pdf");
    auto destFilePath = PathUtils::concatenateFilePath(p_outputDir, fileName);

    bool success = getWebViewExporter(p_option)->doExport(p_option, p_file, destFilePath);
    if (success) {
        outputFile = destFilePath;

        // Copy attachments if available.
        if (p_option.m_exportAttachments) {
            exportAttachments(p_file->getNode(), p_file->getFilePath(), p_outputDir, destFilePath);
        }
    }
    return outputFile;
}

QString Exporter::doExportCustom(const ExportOption &p_option, const QString &p_outputDir, const File *p_file)
{
    Q_ASSERT(p_option.m_customOption);
    QStringList inputFiles;
    QStringList resourcePaths;

    QTemporaryDir tmpDir;
    if (p_option.m_customOption->m_useHtmlInput) {
        // Export to HTML to a tmp dir first.
        if (!tmpDir.isValid()) {
            emit logRequested(tr("Failed to create temporary directory to hold HTML files."));
            return QString();
        }

        auto tmpOption(getExportOptionForIntermediateHtml(p_option, tmpDir.path()));
        auto htmlFile = doExport(tmpOption, tmpDir.path(), p_file);
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
        inputFiles << p_file->getContentPath();
        resourcePaths << p_file->getResourcePath();
    }

    auto fileName = FileUtils::generateFileNameWithSequence(p_outputDir,
                                                            QFileInfo(p_file->getName()).completeBaseName(),
                                                            p_option.m_customOption->m_targetSuffix);
    auto destFilePath = PathUtils::concatenateFilePath(p_outputDir, fileName);

    bool success = doExportCustom(p_option,
                                  inputFiles,
                                  resourcePaths,
                                  destFilePath);
    if (success) {
        // Copy attachments if available.
        if (p_option.m_exportAttachments) {
            exportAttachments(p_file->getNode(), p_file->getFilePath(), p_outputDir, destFilePath);
        }

        return destFilePath;
    }

    return QString();
}

ExportOption Exporter::getExportOptionForIntermediateHtml(const ExportOption &p_option, const QString &p_outputDir)
{
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
    if (p_option.m_targetFormat == ExportFormat::Custom && p_option.m_customOption->m_targetPageScrollable) {
        tmpOption.m_htmlOption.m_scrollable = true;
    }
    tmpOption.m_outputDir = p_outputDir;
    return tmpOption;
}

bool Exporter::doExportCustom(const ExportOption &p_option,
                              const QStringList &p_files,
                              const QStringList &p_resourcePaths,
                              const QString &p_filePath)
{
    const auto cmd = evaluateCommand(p_option,
                                     p_files,
                                     p_resourcePaths,
                                     p_filePath);

    emit logRequested(tr("Custom command: %1").arg(cmd));
    qDebug() << "custom export" << cmd;

    auto state = ProcessUtils::start(cmd,
                                     [this](const QString &msg) {
                                         emit logRequested(msg);
                                     },
                                     m_askedToStop);

    return state == ProcessUtils::Succeeded;
}

QString Exporter::evaluateCommand(const ExportOption &p_option,
                                  const QStringList &p_files,
                                  const QStringList &p_resourcePaths,
                                  const QString &p_filePath)
{
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
                // Deduplicate.
                duplicated = true;
                break;
            }
        }

        if (duplicated) {
            continue;
        }

        if (i > 0) {
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

QString Exporter::getQuotedPath(const QString &p_path)
{
    return QString("\"%1\"").arg(QDir::toNativeSeparators(p_path));
}

void Exporter::collectFiles(const QList<QSharedPointer<File>> &p_files, QStringList &p_inputFiles, QStringList &p_resourcePaths)
{
    for (const auto &file : p_files) {
        p_inputFiles << file->getContentPath();
        p_resourcePaths << file->getResourcePath();
    }
}
