#include "contentmediautils.h"

#include <QDebug>
#include <QSet>
#include <QFileInfo>
#include <QDir>
#include <QHash>

#include <notebookbackend/inotebookbackend.h>
#include <notebook/node.h>

#include <buffer/filetypehelper.h>

#include <vtextedit/markdownutils.h>

#include <utils/pathutils.h>
#include <utils/fileutils.h>
#include <core/file.h>

using namespace vnotex;

void ContentMediaUtils::copyMediaFiles(Node *p_node,
                                       INotebookBackend *p_backend,
                                       const QString &p_destFilePath)
{
    Q_ASSERT(p_node->hasContent());
    auto file = p_node->getContentFile();
    if (file->getContentType().isMarkdown()) {
        copyMarkdownMediaFiles(file->read(),
                               PathUtils::parentDirPath(file->getContentPath()),
                               p_backend,
                               p_destFilePath);
    }
}

void ContentMediaUtils::copyMediaFiles(const QString &p_filePath,
                                       INotebookBackend *p_backend,
                                       const QString &p_destFilePath)
{
    const auto &fileType = FileTypeHelper::getInst().getFileType(p_filePath);
    if (fileType.isMarkdown()) {
        copyMarkdownMediaFiles(FileUtils::readTextFile(p_filePath),
                               PathUtils::parentDirPath(p_filePath),
                               p_backend,
                               p_destFilePath);
    }
}

void ContentMediaUtils::copyMediaFiles(const File *p_file,
                                       const QString &p_destFilePath)
{
    if (p_file->getContentType().isMarkdown()) {
        copyMarkdownMediaFiles(p_file->read(),
                               p_file->getResourcePath(),
                               nullptr,
                               p_destFilePath);
    }
}

void ContentMediaUtils::copyMarkdownMediaFiles(const QString &p_content,
                                               const QString &p_basePath,
                                               INotebookBackend *p_backend,
                                               const QString &p_destFilePath)
{
    auto content = p_content;

    // Images.
    const auto images =
        vte::MarkdownUtils::fetchImagesFromMarkdownText(content,
                                                        p_basePath,
                                                        vte::MarkdownLink::TypeFlag::LocalRelativeInternal);

    QDir destDir(PathUtils::parentDirPath(p_destFilePath));
    QSet<QString> handledImages;
    QHash<QString, QString> renamedImages;
    int lastPos = content.size();
    for (const auto &link : images) {
        Q_ASSERT(link.m_urlInLinkPos < lastPos);
        lastPos = link.m_urlInLinkPos;

        if (handledImages.contains(link.m_path)) {
            auto it = renamedImages.find(link.m_path);
            if (it != renamedImages.end()) {
                content.replace(link.m_urlInLinkPos, link.m_urlInLink.size(), it.value());
            }
            continue;
        }

        handledImages.insert(link.m_path);

        if (!QFileInfo::exists(link.m_path)) {
            qWarning() << "image of Markdown file does not exist" << link.m_path << link.m_urlInLink;
            continue;
        }

        // Get the relative path of the image and apply it to the dest file path.
        const auto oldDestFilePath = destDir.filePath(link.m_urlInLink);
        destDir.mkpath(PathUtils::parentDirPath(oldDestFilePath));
        auto destFilePath = p_backend ? p_backend->renameIfExistsCaseInsensitive(oldDestFilePath)
                                      : FileUtils::renameIfExistsCaseInsensitive(oldDestFilePath);
        if (oldDestFilePath != destFilePath) {
            // Rename happens.
            const auto oldFileName = PathUtils::fileName(oldDestFilePath);
            const auto newFileName = PathUtils::fileName(destFilePath);
            qWarning() << QString("image name conflicts when copy. Renamed from (%1) to (%2)").arg(oldFileName, newFileName);

            // Update the text content.
            auto newUrlInLink(link.m_urlInLink);
            newUrlInLink.replace(newUrlInLink.size() - oldFileName.size(),
                                 oldFileName.size(),
                                 newFileName);

            content.replace(link.m_urlInLinkPos, link.m_urlInLink.size(), newUrlInLink);
            renamedImages.insert(link.m_path, newUrlInLink);
        }

        if (p_backend) {
            p_backend->copyFile(link.m_path, destFilePath);
        } else {
            FileUtils::copyFile(link.m_path, destFilePath);
        }
    }

    if (!renamedImages.isEmpty()) {
        if (p_backend) {
            p_backend->writeFile(p_destFilePath, content);
        } else {
            FileUtils::writeFile(p_destFilePath, content);
        }
    }
}

void ContentMediaUtils::removeMediaFiles(Node *p_node)
{
    Q_ASSERT(p_node->hasContent());
    auto file = p_node->getContentFile();
    if (file->getContentType().isMarkdown()) {
        removeMarkdownMediaFiles(file.data(), p_node->getBackend());
    }
}

void ContentMediaUtils::removeMarkdownMediaFiles(const File *p_file, INotebookBackend *p_backend)
{
    auto content = p_file->read();

    // Images.
    const auto images =
        vte::MarkdownUtils::fetchImagesFromMarkdownText(content,
                                                        p_file->getResourcePath(),
                                                        vte::MarkdownLink::TypeFlag::LocalRelativeInternal);

    QSet<QString> handledImages;
    for (const auto &link : images) {
        if (handledImages.contains(link.m_path)) {
            continue;
        }

        handledImages.insert(link.m_path);

        if (!QFileInfo::exists(link.m_path)) {
            qWarning() << "Image of Markdown file does not exist" << link.m_path << link.m_urlInLink;
            continue;
        }
        p_backend->removeFile(link.m_path);
    }
}

void ContentMediaUtils::copyAttachment(Node *p_node,
                                       INotebookBackend *p_backend,
                                       const QString &p_destFilePath,
                                       const QString &p_destAttachmentFolderPath)
{
    Q_ASSERT(p_node->hasContent());
    Q_ASSERT(!p_node->getAttachmentFolder().isEmpty());

    // Copy the whole folder.
    const auto srcAttachmentFolderPath = p_node->fetchAttachmentFolderPath();
    if (p_backend) {
        p_backend->copyDir(srcAttachmentFolderPath, p_destAttachmentFolderPath);
    } else {
        FileUtils::copyDir(srcAttachmentFolderPath, p_destAttachmentFolderPath);
    }

    // Check if we need to modify links in content.
    // FIXME: check the whole relative path.
    if (p_node->getAttachmentFolder() == PathUtils::dirName(p_destAttachmentFolderPath)) {
        return;
    }

    auto file = p_node->getContentFile();
    if (file->getContentType().isMarkdown()) {
        fixMarkdownLinks(srcAttachmentFolderPath, p_backend, p_destFilePath, p_destAttachmentFolderPath);
    }
}

void ContentMediaUtils::fixMarkdownLinks(const QString &p_srcFolderPath,
                                         INotebookBackend *p_backend,
                                         const QString &p_destFilePath,
                                         const QString &p_destFolderPath)
{
    // TODO.
    Q_UNUSED(p_srcFolderPath);
    Q_UNUSED(p_backend);
    Q_UNUSED(p_destFilePath);
    Q_UNUSED(p_destFolderPath);
}
