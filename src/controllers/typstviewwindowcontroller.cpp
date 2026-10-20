#include "typstviewwindowcontroller.h"

#include <QBuffer>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QPointer>
#include <QStringView>
#include <QVarLengthArray>

#include <core/fileopensettings.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/notebookcoreservice.h>
#include <gui/utils/imageutils.h>
#include <net/networkutils.h>
#include <utility>
#include <utils/fileutils2.h>
#include <vxcore/notebook_json_keys.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace vnotex;

namespace {
// As in legacy image migration, Qt canonical paths alone do not resolve NTFS junctions.
// Unlike that comparison helper, unresolved paths must fail closed here.
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

// Match the native helper's project paths, without normalizing forbidden input away.
bool isProjectPath(const QString &p_path) {
  if (p_path.isEmpty() || p_path.startsWith(QLatin1Char('/')) ||
      p_path.startsWith(QLatin1Char('@')) || p_path.contains(QChar::Null) ||
      p_path.contains(QLatin1Char('\\')) || p_path.contains(QLatin1Char(':'))) {
    return false;
  }
  const QStringView path(p_path);
  qsizetype start = 0;
  while (start <= path.size()) {
    const auto slash = path.indexOf(QLatin1Char('/'), start);
    const auto end = slash < 0 ? path.size() : slash;
    const auto segment = path.mid(start, end - start);
    if (segment.isEmpty() || segment == QLatin1String(".") || segment == QLatin1String("..")) {
      return false;
    }
    if (slash < 0) {
      return true;
    }
    start = slash + 1;
  }
  return false;
}

bool externalSchemeAllowed(const QUrl &p_url) {
  const auto scheme = p_url.scheme();
  return scheme == QStringLiteral("http") || scheme == QStringLiteral("https") ||
         scheme == QStringLiteral("ftp") || scheme == QStringLiteral("mailto");
}

bool containsNull(const QUrl &p_url) {
  // Check every component, including encoded NULs in queries and fragments.
  return p_url.toString(QUrl::FullyEncoded).contains(QLatin1String("%00"), Qt::CaseInsensitive);
}

bool isLocalLinkPath(const QString &p_path) {
  if (p_path.isEmpty() || p_path.contains(QChar::Null) || p_path.startsWith(QLatin1String("//")) ||
      p_path.contains(QLatin1Char('\\'))) {
    return false;
  }
  // A drive-relative path must never resolve against the process's current drive directory.
  return !(p_path.size() >= 2 && p_path.at(1) == QLatin1Char(':') && !QDir::isAbsolutePath(p_path));
}

constexpr qint64 c_imageByteLimit = 32 * 1024 * 1024;

bool pathInside(const QString &p_root, const QString &p_path, bool p_allowRoot = false) {
  if (p_root.isEmpty() || p_path.isEmpty() || !QDir::isAbsolutePath(p_path)) {
    return false;
  }
  const auto relative = QDir(p_root).relativeFilePath(p_path);
  return !QDir::isAbsolutePath(relative) && relative != QLatin1String("..") &&
         !relative.startsWith(QLatin1String("../")) &&
         (p_allowRoot || relative != QLatin1String("."));
}

// Buffer2's folder getters create directories. The notebook query uses the same
// FolderManager resolver as storage, but does not create anything (also for raw notes).
QString resourceFolder(ServiceLocator &p_services, const Buffer2 &p_buffer) {
  const auto &node = p_buffer.nodeId();
  if (!node.notebookId.isEmpty()) {
    return p_services.get<NotebookCoreService>()->getAttachmentsFolder(node.notebookId,
                                                                       node.relativePath);
  }
  // ExternalBufferProvider has no configurable folder and no noncreating getter.
  // Match its filesystem filename().stem(), including leading-dot-only filenames.
  const QFileInfo file(QDir::cleanPath(p_buffer.resolvedPath()));
  auto stem = file.fileName();
  const auto dot = stem.lastIndexOf(QLatin1Char('.'));
  if (dot > 0) {
    stem.truncate(dot);
  }
  return file.dir().filePath(stem + QStringLiteral("_assets"));
}

bool availableResourceFolder(const QString &p_folder) {
  if (p_folder.isEmpty() || p_folder.contains(QChar::Null) || !QDir::isAbsolutePath(p_folder)) {
    return false;
  }
  auto current = QDir::cleanPath(p_folder);
  for (;;) {
    const QFileInfo info(current);
    if (info.exists() || info.isSymLink() || info.isJunction()) {
      return info.isDir() && !resolvedFileSystemPath(current).isEmpty();
    }
    const auto parent = info.absolutePath();
    if (parent == current) {
      return false;
    }
    current = parent;
  }
}

bool containedImageFolder(const QString &p_root, const QString &p_folder) {
  if (p_folder.isEmpty() || p_folder.contains(QChar::Null) || !QDir::isAbsolutePath(p_folder)) {
    return false;
  }
  const auto folder = QDir::cleanPath(QDir::fromNativeSeparators(p_folder));
  if (!pathInside(p_root, folder, true)) {
    return false;
  }
  // The backend lexically normalizes configured paths before mkdir or file writes;
  // inspect that same path rather than a separately interpreted configuration value.
  // Inspect every existing component, not just the leaf: an intermediate junction may
  // leave the project even when a later junction points back into it. Missing suffixes
  // are permitted only after their nearest existing ancestor has passed these checks.
  auto current = p_root;
  const auto relative = QDir(p_root).relativeFilePath(folder);
  const auto components = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  for (qsizetype idx = 0;; ++idx) {
    const QFileInfo info(current);
    if (!info.exists() && !info.isSymLink() && !info.isJunction()) {
      return idx > 0;
    }
    if (!info.isDir() || !pathInside(p_root, resolvedFileSystemPath(current), true)) {
      return false;
    }
    if (idx == components.size()) {
      return true;
    }
    current = QDir(current).filePath(components.at(idx));
  }
}

QUrl insertionSourceUrl(const QString &p_root, const QString &p_source) {
  if (p_source.isEmpty() || p_source.contains(QChar::Null)) {
    return {};
  }
  const auto path = QDir::fromNativeSeparators(p_source);
  // Native absolute filenames must precede URI parsing: '#' and '%' are filename
  // characters here, and a Windows drive letter is not a URI scheme.
  if (QDir::isAbsolutePath(path)) {
    return QUrl::fromLocalFile(path);
  }
  if (path.size() >= 2 && path.at(0).isLetter() && path.at(1) == QLatin1Char(':')) {
    return {};
  }
  QUrl url(p_source, QUrl::StrictMode);
  if (url.scheme().isEmpty() && !path.startsWith(QLatin1String("//")) &&
      QFileInfo(QDir(p_root).filePath(path)).isFile()) {
    // Typed relative filenames are also literal when that exact file exists. URI
    // query/fragment components otherwise remain available for encoded input.
    url = QUrl();
    url.setPath(path, QUrl::DecodedMode);
  }
  return url.isValid() && !containsNull(url) ? url : QUrl();
}

QString localInsertionPath(const QString &p_root, const QUrl &p_url, bool p_linkPolicy = false) {
  if (p_url.isEmpty() || !p_url.isValid() || containsNull(p_url) ||
      (!p_url.scheme().isEmpty() && !p_url.isLocalFile()) ||
      ((!p_url.isLocalFile() || p_linkPolicy) && !p_url.authority().isEmpty())) {
    return {};
  }
  const auto path = QDir::fromNativeSeparators(
      p_url.isLocalFile() ? p_url.toLocalFile() : p_url.path(QUrl::FullyDecoded));
  if (path.isEmpty() || path.contains(QChar::Null) ||
      (path.size() >= 2 && path.at(1) == QLatin1Char(':') && !QDir::isAbsolutePath(path)) ||
      (p_linkPolicy && !isLocalLinkPath(path))) {
    return {};
  }
  return QDir::cleanPath(QDir(p_root).absoluteFilePath(path));
}

bool nativeImageFormat(const QByteArray &p_data) {
  QBuffer input;
  input.setData(p_data);
  if (!input.open(QIODevice::ReadOnly)) {
    return false;
  }
  QImageReader reader(&input);
  reader.setDecideFormatFromContent(true);
  const auto format = reader.format().toLower();
  return format == QByteArrayLiteral("png") || format == QByteArrayLiteral("jpeg") ||
         format == QByteArrayLiteral("jpg") || format == QByteArrayLiteral("gif") ||
         format == QByteArrayLiteral("webp") || format == QByteArrayLiteral("svg");
}

bool typstImageData(const QByteArray &p_data, QByteArray &p_output) {
  QByteArray mime;
  if (p_data.isEmpty() || p_data.size() > c_imageByteLimit ||
      !ImageUtils::protectedImageData(p_data, p_output, mime)) {
    return false;
  }
  if (mime == QByteArrayLiteral("image/bmp")) {
    // The shared validator already checked dimensions before decoding. Typst cannot
    // consume BMP, so encode the validated pixels as PNG before storage or preview.
    const auto image = QImage::fromData(p_output, "BMP");
    p_output.clear();
    QBuffer output(&p_output);
    if (image.isNull() || image.width() > 8192 || image.height() > 8192 ||
        qint64(image.width()) * image.height() > 16 * 1024 * 1024 ||
        !output.open(QIODevice::WriteOnly) || !image.save(&output, "PNG")) {
      p_output.clear();
      return false;
    }
  } else if (mime != QByteArrayLiteral("image/png") && mime != QByteArrayLiteral("image/jpeg") &&
             mime != QByteArrayLiteral("image/gif") && mime != QByteArrayLiteral("image/webp")) {
    p_output.clear();
    return false;
  }
  if (p_output.isEmpty() || p_output.size() > c_imageByteLimit) {
    p_output.clear();
    return false;
  }
  return true;
}

bool rollbackResource(ServiceLocator &p_services, Buffer2 &p_buffer, const QString &p_originalPath,
                      const QString &p_folder, const QString &p_resource, bool p_attachment) {
  // A retargeted handle must never delete an identically named preexisting resource
  // at its new destination. In that case report incomplete cleanup, not success.
  const auto current = p_services.get<BufferService>()->getBufferHandle(p_buffer.id());
  if (!current.isValid() || current.nodeId() != p_buffer.nodeId() ||
      current.resolvedPath() != p_originalPath || resourceFolder(p_services, current) != p_folder) {
    return false;
  }
  return p_attachment ? p_buffer.deleteAttachment(p_resource) : p_buffer.deleteAsset(p_resource);
}
} // namespace

TypstViewWindowController::TypstViewWindowController(ServiceLocator &p_services, QObject *p_parent)
    : TextViewWindowController(p_services, p_parent), m_services(p_services) {}

bool TypstViewWindowController::setBuffer(const Buffer2 &p_buffer, QString *p_error) {
  ++m_generation;
  invalidateInsertionRequests();
  m_insertionBufferPath.clear();
  m_pendingExternalLink = QUrl();
  m_pendingExternalGeneration = 0;
  m_buffer = Buffer2();
  m_project = Project();
  if (p_error) {
    p_error->clear();
  }

  const auto fail = [p_error](const QString &p_message) {
    if (p_error) {
      *p_error = p_message;
    }
    return false;
  };
  if (!p_buffer.isValid() || p_buffer.isEncrypted() || p_buffer.nodeId().isVirtual()) {
    return fail(tr("Typst requires an available, unencrypted file buffer"));
  }
  const auto info = p_buffer.getBuffer();
  if (info.isEmpty() || info.value(QStringLiteral("isVirtual")).toBool()) {
    return fail(tr("Typst requires an available, unencrypted file buffer"));
  }

  const auto path = p_buffer.resolvedPath();
  const QFileInfo file(path);
  const auto name = file.fileName();
  if (path.isEmpty() || path.contains(QChar::Null) || !file.isAbsolute() || file.isDir() ||
      !isProjectPath(name)) {
    return fail(tr("Unable to resolve the Typst source file"));
  }
  const auto root = resolvedFileSystemPath(file.dir().absolutePath());
  if (root.isEmpty() || !QFileInfo(root).isDir()) {
    return fail(tr("The Typst project directory is unavailable"));
  }

  m_buffer = p_buffer;
  m_project.root = root;
  m_project.mainFile = name;
  m_insertionBufferPath = path;
  return true;
}

const TypstViewWindowController::Project &TypstViewWindowController::getProject() const {
  return m_project;
}

bool TypstViewWindowController::bufferAvailable() const {
  return m_buffer.isValid() && !m_buffer.isEncrypted() && !m_project.root.isEmpty() &&
         !m_buffer.getBuffer().isEmpty();
}

NodeIdentifier TypstViewWindowController::resolveIndexedFile(const QString &p_absolutePath) const {
  auto *notebooks = m_services.get<NotebookCoreService>();
  const auto resolved = notebooks->resolvePathToNotebook(p_absolutePath);
  NodeIdentifier node;
  node.notebookId = resolved.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
  node.relativePath = resolved.value(QStringLiteral("relativePath")).toString();
  // Path resolution identifies a containing notebook, not necessarily an indexed note.
  if (!node.isValid() || node.relativePath.isEmpty() ||
      notebooks->getFileInfo(node.notebookId, node.relativePath).isEmpty()) {
    return {};
  }
  return node;
}

void TypstViewWindowController::requestOpenSource(const QString &p_file, int p_start, int p_end) {
  if (!bufferAvailable() || !isProjectPath(p_file) || p_start < 0 || p_end < p_start) {
    emit errorOccurred(tr("Unable to open the Typst source location"));
    return;
  }

  const QDir root(m_project.root);
  const QFileInfo file(root.filePath(p_file));
  const auto canonicalPath = resolvedFileSystemPath(file.absoluteFilePath());
  const auto relativePath = root.relativeFilePath(canonicalPath);
  if (!file.isFile() || canonicalPath.isEmpty() || QDir::isAbsolutePath(relativePath) ||
      relativePath == QLatin1String(".") || relativePath == QLatin1String("..") ||
      relativePath.startsWith(QLatin1String("../"))) {
    emit errorOccurred(tr("Unable to open the Typst source location"));
    return;
  }

  auto node = resolveIndexedFile(canonicalPath);
  if (!node.isValid()) {
    node.relativePath = canonicalPath;
  }
  FileOpenSettings settings;
  settings.m_mode = ViewWindowMode::Edit;
  settings.m_forceMode = true;
  settings.m_cursorOffset = p_start;
  // BufferService owns hooks and adopts the target's existing/notebook read-only state.
  const auto generation = m_generation;
  const QPointer<TypstViewWindowController> guard(this);
  const auto opened = m_services.get<BufferService>()->openBuffer(node, settings);
  if (guard && generation == m_generation && !opened.isValid()) {
    emit errorOccurred(tr("Unable to open the Typst source location"));
  }
}

void TypstViewWindowController::requestOpenLink(const QUrl &p_url) {
  if (m_externalConfirmationActive || !bufferAvailable() || p_url.isEmpty() ||
      !m_pendingExternalLink.isEmpty()) {
    return;
  }
  if (!p_url.isValid() || containsNull(p_url)) {
    emit errorOccurred(tr("Unable to open an invalid link"));
    return;
  }
  if (externalSchemeAllowed(p_url)) {
    confirmExternalLink(p_url);
    return;
  }
  if ((!p_url.scheme().isEmpty() && !p_url.isLocalFile()) || !p_url.authority().isEmpty()) {
    emit errorOccurred(tr("This link is not allowed for this note"));
    return;
  }

  const auto path = p_url.isLocalFile() ? p_url.toLocalFile() : p_url.path(QUrl::FullyDecoded);
  if (!isLocalLinkPath(path)) {
    emit errorOccurred(tr("This link is not allowed for this note"));
    return;
  }
  const auto absolutePath = QDir::cleanPath(QDir(m_project.root).absoluteFilePath(path));
  const QFileInfo file(absolutePath);
  if (!file.isFile()) {
    emit errorOccurred(tr("The linked file does not exist"));
    return;
  }
  const auto canonicalPath = resolvedFileSystemPath(absolutePath);
  if (canonicalPath.isEmpty() || !isLocalLinkPath(canonicalPath)) {
    emit errorOccurred(tr("This link is not allowed for this note"));
    return;
  }

  auto node = resolveIndexedFile(canonicalPath);
  if (!node.isValid()) {
    // Unindexed files use the normal external-buffer identity, not an OS launch.
    // ViewAreaController retains ownership of editor selection and file associations.
    node.relativePath = canonicalPath;
  }
  FileOpenSettings settings;
  settings.m_anchor = p_url.fragment(QUrl::FullyDecoded);
  const auto generation = m_generation;
  const QPointer<TypstViewWindowController> guard(this);
  const auto opened = m_services.get<BufferService>()->openBuffer(node, settings);
  if (guard && generation == m_generation && !opened.isValid()) {
    emit errorOccurred(tr("Unable to open the link"));
  }
}

void TypstViewWindowController::confirmExternalLink(const QUrl &p_url) {
  const auto url = p_url;
  m_pendingExternalLink = url;
  m_pendingExternalGeneration = m_generation;
  // Retargeting invalidates consent, but must not admit a new request while the old modal
  // response is on the stack: the public confirmation callback carries only the URL.
  m_externalConfirmationActive = true;
  const QPointer<TypstViewWindowController> guard(this);
  emit externalLinkConfirmationRequested(url);
  if (guard) {
    m_externalConfirmationActive = false;
  }
}

void TypstViewWindowController::openConfirmedExternalLink(const QUrl &p_url) {
  const auto pending = m_pendingExternalLink;
  const auto generation = m_pendingExternalGeneration;
  // Consume before comparing, so cancellation and mismatched callbacks cannot leave consent.
  m_pendingExternalLink = QUrl();
  m_pendingExternalGeneration = 0;
  if (pending.isEmpty() || pending != p_url || generation != m_generation || !bufferAvailable()) {
    return;
  }
  if (!p_url.isValid() || containsNull(p_url) || !externalSchemeAllowed(p_url)) {
    emit errorOccurred(tr("This link is not allowed for this note"));
    return;
  }
  const QPointer<TypstViewWindowController> guard(this);
  const bool opened = QDesktopServices::openUrl(p_url);
  if (guard && generation == m_generation && !opened) {
    emit errorOccurred(tr("Unable to open the link"));
  }
}

void TypstViewWindowController::invalidateInsertionRequests() { ++m_insertionGeneration; }

bool TypstViewWindowController::resourceAvailable() const {
  if (!bufferAvailable() || m_insertionBufferPath.isEmpty()) {
    return false;
  }
  // Buffer2 copies retain their original nodeId. Consult the live record as well,
  // so a backend retarget cannot authorize work through an old handle.
  const auto current = m_services.get<BufferService>()->getBufferHandle(m_buffer.id());
  return current.isValid() && !current.isEncrypted() && current.nodeId() == m_buffer.nodeId() &&
         current.resolvedPath() == m_insertionBufferPath;
}

bool TypstViewWindowController::insertionAvailable() const {
  return resourceAvailable() && !m_buffer.isReadOnly();
}

QImage TypstViewWindowController::decodeImageForViewing(const QByteArray &p_data,
                                                        QString *p_error) const {
  if (p_error) {
    p_error->clear();
  }
  const auto fail = [p_error](const QString &p_message) {
    if (p_error) {
      *p_error = p_message;
    }
    return QImage();
  };
  if (!resourceAvailable()) {
    return fail(tr("The note is not available for image viewing"));
  }
  QByteArray validated;
  QByteArray mime;
  if (!ImageUtils::protectedImageData(p_data, validated, mime)) {
    return fail(tr("Unable to view image: unsupported, invalid or larger than 32 MiB"));
  }
  QBuffer input(&validated);
  input.open(QIODevice::ReadOnly);
  QImageReader reader(&input);
  reader.setDecideFormatFromContent(true);
  reader.setAutoTransform(true);
  const auto image = reader.read();
  if (image.isNull()) {
    return fail(tr("Unable to decode the image"));
  }
  return image;
}

QString TypstViewWindowController::imageResourcePath(const QString &p_projectPath,
                                                     QString *p_error) const {
  if (p_error) {
    p_error->clear();
  }
  const auto fail = [p_error](const QString &p_message) {
    if (p_error) {
      *p_error = p_message;
    }
    return QString();
  };
  if (!resourceAvailable() || !isProjectPath(p_projectPath)) {
    return fail(tr("This image has no available local project file"));
  }
  const QFileInfo file(QDir(m_project.root).filePath(p_projectPath));
  const auto canonical = resolvedFileSystemPath(file.absoluteFilePath());
  if (!file.isFile() || !pathInside(m_project.root, canonical)) {
    return fail(tr("The image file is unavailable or outside the Typst project"));
  }
  return canonical;
}

bool TypstViewWindowController::insertionIsCurrent(quint64 p_generation, const QString &p_bufferId,
                                                   const QString &p_resolvedPath) const {
  return p_generation == m_insertionGeneration && insertionAvailable() &&
         m_buffer.id() == p_bufferId && m_insertionBufferPath == p_resolvedPath;
}

QByteArray TypstViewWindowController::readImageData(const QString &p_source, QString *p_error) {
  if (p_error) {
    p_error->clear();
  }
  const auto fail = [p_error](const QString &p_message) {
    if (p_error) {
      *p_error = p_message;
    }
    return QByteArray();
  };
  if (!insertionAvailable()) {
    return fail(tr("The note is not available for image insertion"));
  }
  const QPointer<TypstViewWindowController> guard(this);
  const auto generation = m_insertionGeneration;
  const auto bufferId = m_buffer.id();
  const auto originalPath = m_buffer.resolvedPath();
  QByteArray bytes;
  bool loaded = false;
  if (!p_source.contains(QChar::Null) &&
      p_source.startsWith(QLatin1String("data:image/"), Qt::CaseInsensitive)) {
    loaded = ImageUtils::decodeImageUrl(p_source, bytes);
  } else {
    const auto url = insertionSourceUrl(m_project.root, p_source);
    const auto path = localInsertionPath(m_project.root, url);
    if (!path.isEmpty()) {
      loaded = ImageUtils::readImageFile(path, bytes);
    } else if (!url.isEmpty() && !url.host().isEmpty() &&
               (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"))) {
      // This synchronous request pumps events. The limit below bounds accepted image
      // bytes, not the transport buffer owned by the shared network implementation.
      auto reply = NetworkAccess::request(url);
      loaded = reply.m_error == QNetworkReply::NoError;
      bytes = std::move(reply.m_data);
    }
  }
  if (!guard || !guard->insertionIsCurrent(generation, bufferId, originalPath)) {
    return fail(tr("The note changed during image acquisition"));
  }
  QByteArray validated;
  if (!loaded || !typstImageData(bytes, validated)) {
    return fail(tr("Unable to read image: unsupported, invalid or larger than 32 MiB"));
  }
  return validated;
}

QString TypstViewWindowController::insertImage(const QByteArray &p_data, const QString &p_name,
                                               QString *p_createdAsset, QString *p_error) {
  if (p_createdAsset) {
    p_createdAsset->clear();
  }
  if (p_error) {
    p_error->clear();
  }
  const auto fail = [p_error](const QString &p_message) {
    if (p_error) {
      *p_error = p_message;
    }
    return QString();
  };
  if (!insertionAvailable()) {
    return fail(tr("The note is not available for image insertion"));
  }
  const QPointer<TypstViewWindowController> guard(this);
  const auto generation = m_insertionGeneration;
  auto buffer = m_buffer;
  const auto bufferId = buffer.id();
  const auto originalPath = buffer.resolvedPath();
  const auto root = m_project.root;
  auto &services = m_services;
  const auto folder = resourceFolder(services, buffer);
  if (folder.isEmpty()) {
    return fail(tr("Unable to resolve the note's image folder"));
  }
  if (!containedImageFolder(root, folder)) {
    return fail(tr("The image folder is outside the Typst project; choose an image folder "
                   "inside the note directory"));
  }
  QByteArray validated;
  if (!typstImageData(p_data, validated)) {
    return fail(tr("Unable to insert image: unsupported, invalid or larger than 32 MiB"));
  }
  const auto name =
      FileUtils2::generateRandomFileName(p_name, ImageUtils::guessImageSuffix(validated));
  const auto asset = buffer.insertAssetRaw(name, validated);
  const auto rollback = [&](const QString &p_message) {
    if (!asset.isEmpty() &&
        !rollbackResource(services, buffer, originalPath, folder, asset, false)) {
      return fail(p_message + QLatin1Char('\n') +
                  tr("Unable to clean up an uncommitted Typst resource") + QStringLiteral(": ") +
                  asset);
    }
    return fail(p_message);
  };
  if (!guard || !guard->insertionIsCurrent(generation, bufferId, originalPath)) {
    return rollback(tr("The note changed during image insertion"));
  }
  if (asset.isEmpty()) {
    return fail(tr("Unable to create the image asset"));
  }
  // The backend owns collision naming and returns the actual asset identifier.
  const auto stored =
      QDir::isAbsolutePath(asset) ? asset : QDir(folder).filePath(QFileInfo(asset).fileName());
  const auto canonical = resolvedFileSystemPath(stored);
  if (resourceFolder(services, buffer) != folder || !containedImageFolder(root, folder) ||
      !QFileInfo(stored).isFile() || !pathInside(root, canonical)) {
    return rollback(tr("Unable to resolve the image asset inside the Typst project"));
  }
  const auto relative = QDir::fromNativeSeparators(QDir(root).relativeFilePath(canonical));
  if (p_createdAsset) {
    *p_createdAsset = asset;
  }
  return relative;
}

QString TypstViewWindowController::imageReference(const QUrl &p_url, QString *p_error) const {
  if (p_error) {
    p_error->clear();
  }
  const auto fail = [p_error](const QString &p_message) {
    if (p_error) {
      *p_error = p_message;
    }
    return QString();
  };
  if (!insertionAvailable()) {
    return fail(tr("The note is not available for image insertion"));
  }
  const auto path = localInsertionPath(m_project.root, p_url);
  const auto canonical = path.isEmpty() ? QString() : resolvedFileSystemPath(path);
  if (path.isEmpty() || !QFileInfo(path).isFile() || !pathInside(m_project.root, canonical)) {
    return fail(tr("An image reference must be an existing file inside the Typst project"));
  }
  QByteArray bytes;
  QByteArray validated;
  QByteArray mime;
  if (!ImageUtils::readImageFile(canonical, bytes) || !nativeImageFormat(bytes) ||
      !ImageUtils::protectedImageData(bytes, validated, mime)) {
    return fail(tr("Unable to reference image: unsupported, invalid or larger than 32 MiB"));
  }
  return QDir::fromNativeSeparators(QDir(m_project.root).relativeFilePath(canonical));
}

QString TypstViewWindowController::linkDestination(const QString &p_source, bool p_relative,
                                                   QString *p_error) const {
  if (p_error) {
    p_error->clear();
  }
  const auto fail = [p_error](const QString &p_message) {
    if (p_error) {
      *p_error = p_message;
    }
    return QString();
  };
  if (!insertionAvailable()) {
    return fail(tr("The note is not available for link insertion"));
  }
  const auto url = insertionSourceUrl(m_project.root, p_source);
  if (url.isEmpty()) {
    return fail(tr("Unable to insert an invalid link"));
  }
  if (externalSchemeAllowed(url)) {
    if ((url.scheme() == QLatin1String("mailto") && url.path().isEmpty()) ||
        (url.scheme() != QLatin1String("mailto") && url.host().isEmpty())) {
      return fail(tr("Unable to insert an invalid link"));
    }
    return url.toString(QUrl::FullyEncoded);
  }
  const auto path = localInsertionPath(m_project.root, url, true);
  if (path.isEmpty() || !QFileInfo(path).isFile()) {
    return fail(tr("The linked file does not exist or this link is not allowed for this note"));
  }
  const auto canonical = resolvedFileSystemPath(path);
  if (canonical.isEmpty() || !isLocalLinkPath(canonical)) {
    return fail(tr("Unable to resolve the linked file"));
  }
  const auto inputPath = url.isLocalFile() ? url.toLocalFile() : url.path(QUrl::FullyDecoded);
  QUrl destination;
  if (p_relative || !QDir::isAbsolutePath(inputPath)) {
    const auto relative = QDir(m_project.root).relativeFilePath(canonical);
    if (QDir::isAbsolutePath(relative)) {
      destination = QUrl::fromLocalFile(canonical);
    } else {
      destination.setPath(QDir::fromNativeSeparators(relative), QUrl::DecodedMode);
    }
  } else {
    destination = QUrl::fromLocalFile(canonical);
  }
  // Preserve URI component boundaries: decoding a query as a whole would turn an
  // encoded '&' into a separator. Raw filenames never acquired these components.
  if (url.hasQuery()) {
    destination.setQuery(url.query(QUrl::FullyEncoded), QUrl::StrictMode);
  }
  if (url.hasFragment()) {
    destination.setFragment(url.fragment(QUrl::FullyEncoded), QUrl::StrictMode);
  }
  return destination.toString(QUrl::FullyEncoded);
}

bool TypstViewWindowController::canAttach(const QUrl &p_url) const {
  if (!insertionAvailable() || !m_buffer.isAttachmentSupported() || !p_url.isLocalFile() ||
      !QDir::isAbsolutePath(p_url.toLocalFile())) {
    return false;
  }
  const auto source = localInsertionPath(m_project.root, p_url);
  const auto folder = resourceFolder(m_services, m_buffer);
  if (source.isEmpty() || !QFileInfo(source).isFile() || !availableResourceFolder(folder) ||
      m_buffer.getResourceBasePath().isEmpty() ||
      pathInside(QDir::cleanPath(folder), source, true)) {
    return false;
  }
  const auto canonicalSource = resolvedFileSystemPath(source);
  const auto canonicalFolder = resolvedFileSystemPath(folder);
  return !canonicalSource.isEmpty() &&
         (canonicalFolder.isEmpty() || !pathInside(canonicalFolder, canonicalSource, true));
}

QString TypstViewWindowController::insertAttachment(const QString &p_path,
                                                    QString *p_createdAttachment,
                                                    QString *p_error) {
  if (p_createdAttachment) {
    p_createdAttachment->clear();
  }
  if (p_error) {
    p_error->clear();
  }
  const auto fail = [p_error](const QString &p_message) {
    if (p_error) {
      *p_error = p_message;
    }
    return QString();
  };
  if (!canAttach(QUrl::fromLocalFile(p_path))) {
    return fail(tr("The selected file cannot be attached to this note"));
  }
  const QPointer<TypstViewWindowController> guard(this);
  const auto generation = m_insertionGeneration;
  auto buffer = m_buffer;
  const auto bufferId = buffer.id();
  const auto originalPath = buffer.resolvedPath();
  auto &services = m_services;
  const auto folder = resourceFolder(services, buffer);
  const auto attachment = buffer.insertAttachment(p_path);
  const auto rollback = [&](const QString &p_message) {
    if (!attachment.isEmpty() &&
        !rollbackResource(services, buffer, originalPath, folder, attachment, true)) {
      return fail(p_message + QLatin1Char('\n') +
                  tr("Unable to clean up an uncommitted Typst resource") + QStringLiteral(": ") +
                  attachment);
    }
    return fail(p_message);
  };
  if (!guard || !guard->insertionIsCurrent(generation, bufferId, originalPath)) {
    return rollback(tr("The note changed during attachment insertion"));
  }
  if (attachment.isEmpty()) {
    return fail(tr("Unable to copy the attachment"));
  }
  if (resourceFolder(services, buffer) != folder) {
    return rollback(tr("The attachment folder changed during insertion"));
  }
  QString error;
  const auto destination = linkDestination(QDir(folder).filePath(attachment), true, &error);
  if (destination.isEmpty()) {
    return rollback(error);
  }
  if (p_createdAttachment) {
    *p_createdAttachment = attachment;
  }
  return destination;
}

bool TypstViewWindowController::readFileContent(const QUrl &p_url, QString *p_text,
                                                QString *p_error) const {
  if (p_text) {
    p_text->clear();
  }
  if (p_error) {
    p_error->clear();
  }
  const auto fail = [p_error](const QString &p_message) {
    if (p_error) {
      *p_error = p_message;
    }
    return false;
  };
  if (!p_text || !insertionAvailable()) {
    return fail(tr("The note is not available for file content insertion"));
  }
  const auto path = localInsertionPath(m_project.root, p_url);
  if (path.isEmpty() || !QFileInfo(path).isFile() ||
      !QMimeDatabase().mimeTypeForFile(path).inherits(QStringLiteral("text/plain"))) {
    return fail(tr("The selected file is not a local plain text file"));
  }
  if (FileUtils2::readTextFile(path, p_text)) {
    p_text->clear();
    return fail(tr("Unable to read the selected text file"));
  }
  return true;
}

QString TypstViewWindowController::quoteString(const QString &p_text) {
  QString result;
  result.reserve(p_text.size() + 2);
  result += QLatin1Char('"');
  for (const auto ch : p_text) {
    switch (ch.unicode()) {
    case '\\':
      result += QStringLiteral("\\\\");
      break;
    case '"':
      result += QStringLiteral("\\\"");
      break;
    case '\n':
      result += QStringLiteral("\\n");
      break;
    case '\r':
      result += QStringLiteral("\\r");
      break;
    case '\t':
      result += QStringLiteral("\\t");
      break;
    default:
      if (ch.unicode() < 0x20) {
        result += QStringLiteral("\\u{") + QString::number(ch.unicode(), 16) + QLatin1Char('}');
      } else {
        result += ch;
      }
      break;
    }
  }
  result += QLatin1Char('"');
  return result;
}

QString TypstViewWindowController::imageMarkup(const QString &p_path, const QString &p_alt,
                                               int p_width, int p_height) {
  auto markup = QStringLiteral("#image(") + quoteString(p_path);
  if (!p_alt.isEmpty()) {
    markup += QStringLiteral(", alt: ") + quoteString(p_alt);
  }
  if (p_width > 0) {
    markup += QStringLiteral(", width: ") + QString::number(p_width * 0.75, 'g', 16) +
              QStringLiteral("pt");
  }
  if (p_height > 0) {
    markup += QStringLiteral(", height: ") + QString::number(p_height * 0.75, 'g', 16) +
              QStringLiteral("pt");
  }
  if (p_width > 0 && p_height > 0) {
    markup += QStringLiteral(", fit: \"contain\"");
  }
  markup += QLatin1Char(')');
  return markup;
}

QString TypstViewWindowController::linkMarkup(const QString &p_destination, const QString &p_text) {
  auto markup = QStringLiteral("#link(") + quoteString(p_destination);
  if (!p_text.isEmpty()) {
    markup += QStringLiteral(", ") + quoteString(p_text);
  }
  markup += QLatin1Char(')');
  return markup;
}
