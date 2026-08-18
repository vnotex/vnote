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
#include <utils/imagedestinationrewriter.h>
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

  // Images. Every local image is COPIED, including reference-style ones, which
  // the old text-search implementation dropped -- silently omitting them from
  // the exported bundle. Only the rewriting below needs a destination span.
  const auto images = vte::MarkdownUtils::fetchImageLinks(
      content, p_basePath, vte::MarkdownLink::TypeFlag::LocalRelativeInternal);

  // Handle the images that CANNOT be rewritten first.
  //
  // A name collision at the destination forces a rename, and a renamed image
  // whose link cannot be updated leaves the bundle pointing at a file that is
  // not there. Claiming the un-renamed name for the un-rewritable links pushes
  // every avoidable rename onto a link that can absorb it. Ordering is
  // irrelevant to this pass because it rewrites nothing.
  QVector<vte::MarkdownLink> ordered;
  ordered.reserve(images.size());
  for (const auto &link : images) {
    if (!link.hasUrlSpan()) {
      ordered.append(link);
    }
  }
  for (const auto &link : images) {
    if (link.hasUrlSpan()) {
      ordered.append(link);
    }
  }

  QDir destDir(PathUtils::parentDirPath(p_destFilePath));
  QSet<QString> handledImages;
  QHash<QString, QString> renamedImages;
  bool rewrote = false;
  int lastPos = content.size();
  for (const auto &link : ordered) {
    // Spanned entries descend, so rewriting one never disturbs a span still to
    // be visited. Spanless entries carry no ordering constraint and were all
    // consumed above, so the assertion only applies once spans start.
    if (link.hasUrlSpan()) {
      Q_ASSERT(link.m_urlStart < lastPos);
      lastPos = link.m_urlStart;
    }

    qDebug() << "link" << link.m_path << link.m_urlInLink;

    if (handledImages.contains(link.m_path)) {
      auto it = renamedImages.find(link.m_path);
      if (it != renamedImages.end()) {
        rewrote = rewriteImageDestination(content, link, it.value()) || rewrote;
      }
      continue;
    }

    handledImages.insert(link.m_path);

    if (!link.m_exists) {
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

      // Replace the RAW destination span, and use its length -- never the
      // cleaned url's. `a\_b.png` occupies 8 source characters and cleans to 7;
      // measuring the replacement with the cleaned length would eat the
      // character after it.
      if (link.hasUrlSpan()) {
        rewrote = rewriteImageDestination(content, link, newUrlInLink) || rewrote;
      } else {
        // A reference-style image whose destination lives in a link reference
        // definition this record cannot locate. The pass ordering above means
        // this is only reachable when two such images collide with each other,
        // which no rewrite could resolve either. Copy the bytes and say so.
        qWarning() << "renamed an image whose link cannot be updated; the exported note will "
                      "still point at"
                   << link.m_urlInLink;
      }
      // The SOURCE-NEUTRAL url, so a later occurrence in the other syntax gets
      // spelled its own way.
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

  if (rewrote) {
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
