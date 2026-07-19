#include "contentmediautils.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <notebookbackend/inotebookbackend.h>

#include <buffer/filetypehelper.h>

#include <vtextedit/markdownutils.h>
#include <vtextedit/textutils.h>

#include <utils/fileutils2.h>
#include <utils/pathutils.h>

using namespace vnotex;

void ContentMediaUtils::copyMediaFiles(const QString &p_filePath, INotebookBackend *p_backend,
                                       const QString &p_destFilePath) {
  const auto &fileType = FileTypeHelper::getInst().getFileType(p_filePath);
  if (fileType.isMarkdown()) {
    QString text;
    Error err = FileUtils2::readTextFile(p_filePath, &text);
    if (err) {
      qWarning() << err.what();
      return;
    }
    copyMarkdownMediaFiles(text, PathUtils::parentDirPath(p_filePath), p_backend, p_destFilePath);
  }
}

void ContentMediaUtils::copyMarkdownMediaFiles(const QString &p_content, const QString &p_basePath,
                                               INotebookBackend *p_backend,
                                               const QString &p_destFilePath) {
  auto content = p_content;

  // Images.
  const auto images = vte::MarkdownUtils::fetchImagesFromMarkdownText(
      content, p_basePath, vte::MarkdownLink::TypeFlag::LocalRelativeInternal);

  QDir destDir(PathUtils::parentDirPath(p_destFilePath));
  QSet<QString> handledImages;
  QHash<QString, QString> renamedImages;
  int lastPos = content.size();
  for (const auto &link : images) {
    Q_ASSERT(link.m_urlInLinkPos < lastPos);
    lastPos = link.m_urlInLinkPos;

    qDebug() << "link" << link.m_path << link.m_urlInLink;

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
    const auto decodedUrlInLink = vte::TextUtils::decodeUrl(link.m_urlInLink);
    const auto oldDestFilePath = destDir.filePath(decodedUrlInLink);
    destDir.mkpath(PathUtils::parentDirPath(oldDestFilePath));
    auto destFilePath = p_backend ? p_backend->renameIfExistsCaseInsensitive(oldDestFilePath)
                                  : FileUtils2::renameIfExistsCaseInsensitive(oldDestFilePath);
    if (oldDestFilePath != destFilePath) {
      // Rename happens.
      const auto oldFileName = PathUtils::fileName(oldDestFilePath);
      const auto newFileName = PathUtils::fileName(destFilePath);
      qWarning() << QStringLiteral("image name conflicts when copy, renamed from (%1) to (%2)")
                        .arg(oldFileName, newFileName);

      // Update the text content.
      const auto encodedOldFileName = PathUtils::fileName(link.m_urlInLink);
      const auto encodedNewFileName = vte::TextUtils::encodeUrl(newFileName);
      auto newUrlInLink(link.m_urlInLink);
      newUrlInLink.replace(newUrlInLink.size() - encodedOldFileName.size(),
                           encodedOldFileName.size(), encodedNewFileName);

      content.replace(link.m_urlInLinkPos, link.m_urlInLink.size(), newUrlInLink);
      renamedImages.insert(link.m_path, newUrlInLink);
    }

    if (p_backend) {
      p_backend->copyFile(link.m_path, destFilePath);
    } else {
      Error err = FileUtils2::copyFile(link.m_path, destFilePath);
      if (err) {
        qWarning() << err.what();
      }
    }
  }

  if (!renamedImages.isEmpty()) {
    if (p_backend) {
      p_backend->writeFile(p_destFilePath, content);
    } else {
      Error err = FileUtils2::writeFile(p_destFilePath, content);
      if (err) {
        qWarning() << err.what();
      }
    }
  }
}
