#include "contentmediautils.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringView>
#include <QVarLengthArray>

#include <algorithm>
#include <exception>

#include <notebookbackend/inotebookbackend.h>

#include <buffer/filetypehelper.h>

#include <vtextedit/markdownutils.h>
#include <vtextedit/textutils.h>

#include <utils/fileutils2.h>
#include <utils/imagedestinationrewriter.h>
#include <utils/pathutils.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace vnotex;

namespace {
bool pathEntryExists(const QFileInfo &p_info) {
  return p_info.exists() || p_info.isSymLink() || p_info.isJunction();
}

bool pathInside(const QString &p_root, const QString &p_path) {
  const auto sensitivity =
      FileUtils2::isPlatformNameCaseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive;
  return !p_root.isEmpty() && p_path.startsWith(p_root, sensitivity) &&
         (p_path.size() == p_root.size() || p_root.endsWith(QLatin1Char('/')) ||
          p_path.at(p_root.size()) == QLatin1Char('/'));
}

QString pathKey(const QString &p_path) {
  return FileUtils2::isPlatformNameCaseSensitive() ? p_path : p_path.toCaseFolded();
}

// Match the controller's filesystem containment policy: Qt canonical paths alone
// do not resolve Windows junctions, so resolve the final handle path there.
QString resolvedFileSystemPath(const QString &p_path) {
#ifdef Q_OS_WIN
  const auto native = QDir::toNativeSeparators(p_path);
  const HANDLE handle = ::CreateFileW(reinterpret_cast<const wchar_t *>(native.utf16()), 0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return {};
  }
  QVarLengthArray<wchar_t, MAX_PATH> buffer(MAX_PATH);
  DWORD written = ::GetFinalPathNameByHandleW(handle, buffer.data(), buffer.size(),
                                              FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (written >= static_cast<DWORD>(buffer.size())) {
    buffer.resize(static_cast<int>(written) + 1);
    written = ::GetFinalPathNameByHandleW(handle, buffer.data(), buffer.size(),
                                          FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  }
  ::CloseHandle(handle);
  if (written == 0 || written >= static_cast<DWORD>(buffer.size())) {
    return {};
  }
  auto result = QString::fromWCharArray(buffer.data(), static_cast<int>(written));
  if (result.startsWith(QStringLiteral("\\\\?\\UNC\\"))) {
    result = QStringLiteral("\\\\") + result.mid(8);
  } else if (result.startsWith(QStringLiteral("\\\\?\\"))) {
    result.remove(0, 4);
  }
  return QDir::fromNativeSeparators(result);
#else
  return QFileInfo(p_path).canonicalFilePath();
#endif
}

// Resolve a possibly unpublished output directory through its nearest existing
// ancestor. No mkdir may happen before the image paths have been checked.
QString resolvedDirectory(const QString &p_path) {
  auto current = p_path;
  QString suffix;
  for (;;) {
    const QFileInfo info(current);
    if (pathEntryExists(info)) {
      const auto resolved = info.isDir() ? resolvedFileSystemPath(current) : QString();
      return resolved.isEmpty() ? QString() : QDir::cleanPath(resolved + suffix);
    }
    const auto parent = info.absolutePath();
    if (parent == current) {
      return {};
    }
    suffix.prepend(QLatin1Char('/') + info.fileName());
    current = parent;
  }
}

enum class ContainedPath { Valid, Outside, Invalid };

ContainedPath resolveContainedPath(const QString &p_root, const QString &p_relativePath,
                                   QString *p_resolved) {
  auto current = p_root;
  qsizetype start = 0;
  while (start < p_relativePath.size()) {
    const auto slash = p_relativePath.indexOf(QLatin1Char('/'), start);
    const auto end = slash < 0 ? p_relativePath.size() : slash;
    if (!current.endsWith(QLatin1Char('/'))) {
      current.append(QLatin1Char('/'));
    }
    current.append(QStringView(p_relativePath).mid(start, end - start));
    const QFileInfo info(current);
    if (!pathEntryExists(info)) {
      *p_resolved = current + p_relativePath.mid(end);
      return ContainedPath::Valid;
    }
    const auto resolved = resolvedFileSystemPath(current);
    if (resolved.isEmpty()) {
      return ContainedPath::Invalid;
    }
    if (!pathInside(p_root, resolved)) {
      return ContainedPath::Outside;
    }
    if (slash >= 0 && !info.isDir()) {
      return ContainedPath::Invalid;
    }
    // Check every ancestor, including an escape followed by a link back inside.
    current = resolved;
    start = end + 1;
  }
  *p_resolved = current;
  return ContainedPath::Valid;
}

QString copyImageFile(const QString &p_sourcePath, const QString &p_requestedDestination,
                      INotebookBackend *p_backend, bool p_renameOnCollision, QString *p_error) {
  if (p_error) {
    p_error->clear();
  }
  const auto fail = [p_error](const QString &p_message) {
    if (p_error) {
      *p_error = p_message;
    }
    return QString();
  };
  const auto parent = QFileInfo(p_requestedDestination).absolutePath();
  if (!QDir().mkpath(parent)) {
    return fail(QStringLiteral("Failed to create image directory: %1").arg(parent));
  }
  try {
    const auto destination =
        p_renameOnCollision
            ? (p_backend ? p_backend->renameIfExistsCaseInsensitive(p_requestedDestination)
                         : FileUtils2::renameIfExistsCaseInsensitive(p_requestedDestination))
            : p_requestedDestination;
    // FileUtils2::copyFile overwrites. Even a dangling link is a collision, not
    // permission to replace an entry that this export did not create.
    if (pathEntryExists(QFileInfo(destination))) {
      return fail(QStringLiteral("Image destination already exists: %1").arg(destination));
    }
    if (p_backend) {
      p_backend->copyFile(p_sourcePath, destination);
    } else {
      const auto error = FileUtils2::copyFile(p_sourcePath, destination);
      if (error) {
        return fail(error.what());
      }
    }
    return destination;
  } catch (const std::exception &error) {
    return fail(QString::fromUtf8(error.what()));
  }
}
} // namespace

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
    QString error;
    const auto destFilePath = copyImageFile(link.m_path, oldDestFilePath, p_backend, true, &error);
    if (destFilePath.isEmpty()) {
      qWarning().noquote() << error;
      continue;
    }
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

bool ContentMediaUtils::copyRelativeImageFiles(const QStringList &p_decodedPaths,
                                               const QString &p_basePath,
                                               const QString &p_destFilePath,
                                               QStringList *p_warnings) {
  if (std::all_of(p_decodedPaths.cbegin(), p_decodedPaths.cend(),
                  [](const QString &p_path) { return p_path.isEmpty(); })) {
    return true;
  }
  const auto warn = [p_warnings](const QString &p_message) {
    if (p_warnings) {
      p_warnings->append(p_message);
    } else {
      qWarning().noquote() << p_message;
    }
  };
  const auto sourceRoot = p_basePath.isEmpty() || p_basePath.contains(QChar::Null)
                              ? QString()
                              : resolvedFileSystemPath(p_basePath);
  if (sourceRoot.isEmpty() || !QFileInfo(sourceRoot).isDir()) {
    warn(QStringLiteral("Image source directory is unavailable: %1").arg(p_basePath));
    return false;
  }
  if (p_destFilePath.isEmpty() || p_destFilePath.contains(QChar::Null)) {
    warn(QStringLiteral("Image output file path is invalid: %1").arg(p_destFilePath));
    return false;
  }
  const QFileInfo outputInfo(QDir::cleanPath(QDir::fromNativeSeparators(p_destFilePath)));
  const auto destinationRoot = resolvedDirectory(outputInfo.absolutePath());
  QString reservedOutput;
  if (destinationRoot.isEmpty() || resolveContainedPath(destinationRoot, outputInfo.fileName(),
                                                        &reservedOutput) != ContainedPath::Valid) {
    warn(
        QStringLiteral("Image output directory is unavailable: %1").arg(outputInfo.absolutePath()));
    return false;
  }

  QSet<QPair<QString, QString>> copied;
  for (const auto &path : p_decodedPaths) {
    if (path.isEmpty()) {
      continue;
    }
    const auto originalRelative = QDir::fromNativeSeparators(path);
    const auto relative = QDir::cleanPath(originalRelative);
    if (path.contains(QChar::Null) || QDir::isAbsolutePath(relative) ||
#ifdef Q_OS_WIN
        relative.contains(QLatin1Char(':')) ||
#endif
        relative == QLatin1String("..") || relative.startsWith(QLatin1String("../"))) {
      warn(QStringLiteral("Image path is outside the source directory: %1").arg(path));
      continue;
    }
    // Cleaning must not erase an existing escaping ancestor before containment checks.
    // Still permit ordinary internal normalization, including nonexistent a/../ segments.
    if (originalRelative != relative) {
      QString resolved;
      if (resolveContainedPath(sourceRoot, originalRelative, &resolved) == ContainedPath::Outside) {
        warn(QStringLiteral("Image path is outside the source directory: %1").arg(path));
        continue;
      }
      if (resolveContainedPath(destinationRoot, originalRelative, &resolved) ==
          ContainedPath::Outside) {
        warn(QStringLiteral("Image path is outside the output directory: %1").arg(path));
        continue;
      }
    }
    QString source;
    const auto sourceState = resolveContainedPath(sourceRoot, relative, &source);
    if (sourceState == ContainedPath::Outside) {
      warn(QStringLiteral("Image path is outside the source directory: %1").arg(path));
      continue;
    }
    if (sourceState != ContainedPath::Valid || !QFileInfo(source).isFile()) {
      warn(QStringLiteral("Image is missing or not a file: %1").arg(path));
      continue;
    }
    QString destination;
    const auto destinationState = resolveContainedPath(destinationRoot, relative, &destination);
    if (destinationState == ContainedPath::Outside) {
      warn(QStringLiteral("Image path is outside the output directory: %1").arg(path));
      continue;
    }
    if (destinationState != ContainedPath::Valid) {
      warn(QStringLiteral("Image destination is incompatible: %1").arg(path));
      return false;
    }
    if (pathInside(reservedOutput, destination)) {
      warn(QStringLiteral("Image destination conflicts with the source output: %1").arg(path));
      return false;
    }
    const auto pair = qMakePair(pathKey(source), pathKey(destination));
    if (copied.contains(pair)) {
      continue;
    }
    QString error;
    if (copyImageFile(source, destination, nullptr, false, &error).isEmpty()) {
      warn(error);
      return false;
    }
    copied.insert(pair);
  }
  return true;
}
