#include "exporter.h"

#include <QWidget>

#include <notebook/notebook.h>
#include <notebook/node.h>
#include <buffer/buffer.h>
#include <core/file.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <utils/contentmediautils.h>
#include "webviewexporter.h"

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
        emit logRequested(tr("Failed to create output folder %1.").arg(p_option.m_outputDir));
        return outputFile;
    }

    outputFile = doExport(p_option, p_option.m_outputDir, file.data());

    cleanUp();

    return outputFile;
}

QString Exporter::doExportMarkdown(const ExportOption &p_option, const QString &p_outputDir, const File *p_file)
{
    QString outputFile;
    if (!p_file->getContentType().isMarkdown()) {
        emit logRequested(tr("Format %1 is not supported to export as Markdown.").arg(p_file->getContentType().m_displayName));
        return outputFile;
    }

    // Export it to a folder with the same name.
    auto name = FileUtils::generateFileNameWithSequence(p_outputDir, p_file->getName(), "");
    auto outputFolder = PathUtils::concatenateFilePath(p_outputDir, name);
    QDir outDir(outputFolder);
    if (!outDir.mkpath(outputFolder)) {
        emit logRequested(tr("Failed to create output folder %1.").arg(outputFolder));
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
    const auto &attachmentFolder = p_node->getAttachmentFolder();
    if (!attachmentFolder.isEmpty()) {
        auto relativePath = PathUtils::relativePath(PathUtils::parentDirPath(p_srcFilePath),
                                                    p_node->fetchAttachmentFolderPath());
        auto destAttachmentFolderPath = QDir(p_outputFolder).filePath(relativePath);
        destAttachmentFolderPath = FileUtils::renameIfExistsCaseInsensitive(destAttachmentFolderPath);
        ContentMediaUtils::copyAttachment(p_node, nullptr, p_destFilePath, destAttachmentFolderPath);
    }
}

QStringList Exporter::doExport(const ExportOption &p_option, Node *p_folder)
{
    m_askedToStop = false;

    auto outputFiles = doExport(p_option, p_option.m_outputDir, p_folder);

    cleanUp();

    return outputFiles;
}

QStringList Exporter::doExport(const ExportOption &p_option, const QString &p_outputDir, Node *p_folder)
{
    Q_ASSERT(p_folder->isContainer());

    QStringList outputFiles;

    // Make path.
    auto name = FileUtils::generateFileNameWithSequence(p_outputDir, p_folder->getName());
    auto outputFolder = PathUtils::concatenateFilePath(p_outputDir, name);
    if (!QDir().mkpath(outputFolder)) {
        emit logRequested(tr("Failed to create output folder %1.").arg(outputFolder));
        return outputFiles;
    }

    p_folder->load();
    const auto &children = p_folder->getChildren();
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

    // Make path.
    auto name = FileUtils::generateFileNameWithSequence(p_option.m_outputDir,
                                                        tr("notebook_%1").arg(p_notebook->getName()));
    auto outputFolder = PathUtils::concatenateFilePath(p_option.m_outputDir, name);
    if (!QDir().mkpath(outputFolder)) {
        emit logRequested(tr("Failed to create output folder %1.").arg(outputFolder));
        return outputFiles;
    }

    auto rootNode = p_notebook->getRootNode();
    Q_ASSERT(rootNode->isLoaded());

    const auto &children = rootNode->getChildren();
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
