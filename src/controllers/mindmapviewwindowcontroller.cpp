#include "mindmapviewwindowcontroller.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QPointer>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/fileopensettings.h>
#include <core/markdowneditorconfig.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/notebookcoreservice.h>
#include <gui/services/mindmapimageloader.h>
#include <gui/utils/imageutils.h>
#include <net/networkutils.h>
#include <utils/fileutils2.h>
#include <utils/pathutils.h>
#include <vxcore/notebook_json_keys.h>
#include <widgets/editors/linkinsertutils.h>

using namespace vnotex;

namespace {
constexpr qint64 c_imageByteLimit = 32 * 1024 * 1024;

bool relativeResourceUrl(const QUrl &p_url) {
  const auto path = p_url.path(QUrl::FullyDecoded);
  return p_url.isValid() && p_url.isRelative() && p_url.authority().isEmpty() && !path.isEmpty() &&
         !path.contains(QChar::Null) && QDir::isRelativePath(path) &&
         !path.startsWith(QLatin1Char('/')) && !path.startsWith(QLatin1Char('\\'));
}

bool externalSchemeAllowed(const QUrl &p_url, bool p_protected) {
  const auto scheme = p_url.scheme();
  return scheme == QStringLiteral("http") || scheme == QStringLiteral("https") ||
         (!p_protected && (scheme == QStringLiteral("ftp") || scheme == QStringLiteral("mailto")));
}
} // namespace

MindMapViewWindowController::MindMapViewWindowController(ServiceLocator &p_services,
                                                         QObject *p_parent)
    : QObject(p_parent), m_services(p_services),
      m_imageLoader(new MindMapImageLoader(p_services, this)) {
  connect(m_services.get<BufferService>()->asQObject(), SIGNAL(protectedLockingChanged(bool)), this,
          SLOT(onProtectedLockingChanged(bool)));
  connect(m_imageLoader, &MindMapImageLoader::imageReady, this,
          [this](const QString &p_url, quint64 p_requestId, const QImage &p_image) {
            // Loader cancellation rejects prior generations. Recheck authorization
            // as the buffer may have become protected while an image was loading.
            if (bufferAvailable() && !m_buffer.isEncrypted()) {
              emit imageReady(p_url, p_requestId, p_image);
            }
          });
}

MindMapViewWindowController::~MindMapViewWindowController() { invalidateRequests(); }

void MindMapViewWindowController::setBuffer(const Buffer2 &p_buffer) {
  invalidateRequests();
  m_buffer = p_buffer;
}

void MindMapViewWindowController::invalidateRequests() {
  ++m_generation;
  m_pendingExternalLink = QUrl();
  m_pendingExternalGeneration = 0;
  m_imageLoader->cancel();
}

void MindMapViewWindowController::onProtectedLockingChanged(bool p_locking) {
  Q_UNUSED(p_locking);
  if (m_buffer.isEncrypted()) {
    // The widget's later connection clears its native image cache. Revocation
    // must happen first, including when a canceled lock resumes the note.
    invalidateRequests();
  }
}

bool MindMapViewWindowController::bufferAvailable() const {
  return m_buffer.isValid() && !m_buffer.getBuffer().isEmpty() &&
         (!m_buffer.isEncrypted() || !m_services.get<BufferService>()->isProtectedLocking());
}

bool MindMapViewWindowController::isCurrent(
    quint64 p_generation, const std::shared_ptr<ProtectedBufferLease> &p_lease) const {
  return p_generation == m_generation && bufferAvailable() &&
         (!m_buffer.isEncrypted() || (p_lease && p_lease->isCurrent()));
}

void MindMapViewWindowController::requestImage(const QString &p_url, quint64 p_requestId) {
  const auto generation = m_generation;
  const auto lease = m_buffer.isEncrypted() ? m_buffer.acquireProtectedLease() : nullptr;
  if (!isCurrent(generation, lease)) {
    emit imageReady(p_url, p_requestId, QImage());
    return;
  }

  if (!m_buffer.isEncrypted()) {
    m_imageLoader->requestImage(p_url, p_requestId, m_buffer.getResourceBasePath());
    return;
  }

  QByteArray bytes;
  bool loaded = false;
  if (p_url.startsWith(QStringLiteral("data:image/"), Qt::CaseInsensitive)) {
    loaded = ImageUtils::decodeImageUrl(p_url, bytes);
  } else {
    const QUrl source(p_url, QUrl::StrictMode);
    // Only the authenticated buffer's contained resource reader may access
    // protected-note assets. In particular, file: and network URLs never get IO.
    if (relativeResourceUrl(source)) {
      const auto path = QDir::cleanPath(
          QDir(m_buffer.nodeId().parentPath()).filePath(source.path(QUrl::FullyDecoded)));
      VxCoreError error = VXCORE_OK;
      bytes = m_buffer.readResource(path, &error);
      loaded = error == VXCORE_OK && bytes.size() <= c_imageByteLimit;
    }
  }

  const auto image = loaded ? MindMapImageLoader::decodeImage(bytes) : QImage();
  bytes.fill('\0');
  if (isCurrent(generation, lease)) {
    emit imageReady(p_url, p_requestId, image);
  }
}

QByteArray MindMapViewWindowController::readImageData(const QString &p_source, QString *p_error) {
  if (p_error) {
    p_error->clear();
  }
  const auto fail = [p_error](const QString &p_message) {
    if (p_error) {
      *p_error = p_message;
    }
    return QByteArray();
  };
  const QPointer<MindMapViewWindowController> guard(this);
  const auto generation = m_generation;
  const auto lease = m_buffer.isEncrypted() ? m_buffer.acquireProtectedLease() : nullptr;
  if (!isCurrent(generation, lease) || m_buffer.isReadOnly()) {
    return fail(tr("The note is not available for image insertion"));
  }

  QByteArray bytes;
  bool loaded = false;
  if (p_source.startsWith(QStringLiteral("data:image/"), Qt::CaseInsensitive)) {
    loaded = ImageUtils::decodeImageUrl(p_source, bytes);
  } else {
    const auto source = QFileInfo(p_source).isAbsolute() ? QUrl::fromLocalFile(p_source)
                                                         : QUrl(p_source, QUrl::StrictMode);
    if (source.isValid() && source.isLocalFile()) {
      loaded = ImageUtils::readImageFile(source.toLocalFile(), bytes);
    } else if (source.isValid() && (source.scheme() == QStringLiteral("http") ||
                                    source.scheme() == QStringLiteral("https"))) {
      auto reply = NetworkAccess::request(source);
      loaded = reply.m_error == QNetworkReply::NoError;
      bytes = std::move(reply.m_data);
    }
  }
  if (!guard || !isCurrent(generation, lease) || m_buffer.isReadOnly()) {
    bytes.fill('\0');
    return fail(tr("The note changed during image acquisition"));
  }
  QByteArray sanitized;
  QByteArray mime;
  const bool valid = loaded && !bytes.isEmpty() && bytes.size() <= c_imageByteLimit &&
                     ImageUtils::protectedImageData(bytes, sanitized, mime);
  bytes.fill('\0');
  if (!valid) {
    sanitized.fill('\0');
    return fail(tr("Unable to read image: unsupported, invalid or larger than 32 MiB"));
  }
  return sanitized;
}

QString MindMapViewWindowController::insertImage(const QByteArray &p_data, const QString &p_title,
                                                 bool p_base64, QString *p_createdAsset,
                                                 QString *p_error) {
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
  const QPointer<MindMapViewWindowController> guard(this);
  const auto generation = m_generation;
  const auto lease = m_buffer.isEncrypted() ? m_buffer.acquireProtectedLease() : nullptr;
  if (!isCurrent(generation, lease) || m_buffer.isReadOnly()) {
    return fail(tr("The note is not available for image insertion"));
  }
  QByteArray sanitized;
  QByteArray mime;
  if (p_data.isEmpty() || p_data.size() > c_imageByteLimit ||
      !ImageUtils::protectedImageData(p_data, sanitized, mime)) {
    sanitized.fill('\0');
    return fail(tr("Unable to insert image: unsupported, invalid or larger than 32 MiB"));
  }
  if (p_base64) {
    auto encoded = sanitized.toBase64();
    sanitized.fill('\0');
    const auto result = QStringLiteral("data:%1;base64,%2")
                            .arg(QString::fromLatin1(mime), QString::fromLatin1(encoded));
    encoded.fill('\0');
    return result;
  }

  auto buffer = m_buffer;
  const auto base = buffer.getResourceBasePath();
  const auto folder = buffer.getAssetsFolder();
  if (base.isEmpty() || folder.isEmpty()) {
    sanitized.fill('\0');
    return fail(tr("Unable to resolve the note's image folder"));
  }
  const auto name =
      FileUtils2::generateRandomFileName(p_title, ImageUtils::guessImageSuffix(sanitized));
  const auto asset = buffer.insertAssetRaw(name, sanitized);
  sanitized.fill('\0');
  if (!guard || !isCurrent(generation, lease) || m_buffer.isReadOnly()) {
    if (!asset.isEmpty()) {
      buffer.deleteAsset(asset);
    }
    return fail(tr("The note changed during image insertion"));
  }
  if (asset.isEmpty()) {
    return fail(tr("Unable to create the image asset"));
  }
  const auto path =
      QDir::isAbsolutePath(asset) ? asset : QDir(folder).filePath(QFileInfo(asset).fileName());
  const auto url = relativeLink(QUrl::fromLocalFile(path));
  if (url.isEmpty()) {
    buffer.deleteAsset(asset);
    return fail(tr("Unable to resolve the image asset link"));
  }
  if (p_createdAsset) {
    *p_createdAsset = asset;
  }
  return url;
}

QString MindMapViewWindowController::insertAttachment(const QString &p_filePath,
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
  const QPointer<MindMapViewWindowController> guard(this);
  const auto generation = m_generation;
  const auto lease = m_buffer.isEncrypted() ? m_buffer.acquireProtectedLease() : nullptr;
  if (!isCurrent(generation, lease) || m_buffer.isReadOnly() || !m_buffer.isAttachmentSupported()) {
    return fail(tr("The note is not available for attachment insertion"));
  }
  auto buffer = m_buffer;
  const auto folder = buffer.getAttachmentsFolder();
  const QFileInfo source(p_filePath);
  if (folder.isEmpty() || buffer.getResourceBasePath().isEmpty() || !source.isAbsolute() ||
      !source.isFile() || PathUtils::pathContains(folder, p_filePath)) {
    return fail(tr("The selected file cannot be attached to this note"));
  }
  const auto attachment = buffer.insertAttachment(p_filePath);
  if (!guard || !isCurrent(generation, lease) || m_buffer.isReadOnly()) {
    if (!attachment.isEmpty()) {
      buffer.deleteAttachment(attachment);
    }
    return fail(tr("The note changed during attachment insertion"));
  }
  if (attachment.isEmpty()) {
    return fail(tr("Unable to copy the attachment"));
  }
  const auto url = relativeLink(QUrl::fromLocalFile(QDir(folder).filePath(attachment)));
  if (url.isEmpty()) {
    buffer.deleteAttachment(attachment);
    return fail(tr("Unable to resolve the attachment link"));
  }
  if (p_createdAttachment) {
    *p_createdAttachment = attachment;
  }
  return url;
}

QString MindMapViewWindowController::relativeLink(const QUrl &p_url) const {
  const auto base = m_buffer.getResourceBasePath();
  if (!bufferAvailable() || base.isEmpty() || !p_url.isValid() || !p_url.isLocalFile()) {
    return QString();
  }
  const auto path = p_url.toLocalFile();
  if (!QDir::isAbsolutePath(path)) {
    return QString();
  }
  auto relative = QDir::fromNativeSeparators(PathUtils::relativePath(base, path));
  QUrl link;
  if (QDir::isAbsolutePath(relative)) {
    link = QUrl::fromLocalFile(path);
  } else {
    auto *config = m_services.get<ConfigMgr2>();
    if (config &&
        config->getEditorConfig().getMarkdownEditorConfig().getPrependDotInRelativeLink() &&
        !relative.startsWith(QLatin1Char('.'))) {
      relative.prepend(QStringLiteral("./"));
    }
    link.setPath(relative, QUrl::DecodedMode);
  }
  return LinkInsertUtils::appendFragmentToLink(link.toString(QUrl::FullyEncoded),
                                               p_url.fragment(QUrl::FullyEncoded));
}

void MindMapViewWindowController::requestOpenLink(const QString &p_url,
                                                  const FileOpenSettings &p_settings) {
  if (!bufferAvailable() || p_url.isEmpty() || !m_pendingExternalLink.isEmpty()) {
    return;
  }
  const bool protectedBuffer = m_buffer.isEncrypted();
  auto lease = protectedBuffer ? m_buffer.acquireProtectedLease() : nullptr;
  if (!isCurrent(m_generation, lease)) {
    return;
  }
  QUrl url(p_url, QUrl::StrictMode);
  // A dropped absolute path on another Windows drive is a local path, not a URL scheme.
  if (!protectedBuffer && url.scheme().size() == 1 && QFileInfo(p_url).isAbsolute()) {
    url = QUrl::fromLocalFile(p_url);
  }
  if (!url.isValid()) {
    emit errorOccurred(tr("Unable to open an invalid link"));
    return;
  }
  if (externalSchemeAllowed(url, protectedBuffer)) {
    // Never hold a protected operation lease across the widget's modal question.
    lease.reset();
    confirmExternalLink(url);
    return;
  }
  if (protectedBuffer ? !relativeResourceUrl(url)
                      : (!url.scheme().isEmpty() && !url.isLocalFile()) ||
                            (url.isRelative() && !url.authority().isEmpty())) {
    emit errorOccurred(tr("This link is not allowed for this note"));
    return;
  }

  const auto path = url.isLocalFile() ? url.toLocalFile() : url.path(QUrl::FullyDecoded);
  if (path.isEmpty() || path.contains(QChar::Null)) {
    return;
  }
  const auto base = m_buffer.getResourceBasePath();
  if (base.isEmpty()) {
    emit errorOccurred(tr("Unable to resolve the note's link base path"));
    return;
  }
  const auto absolutePath = QDir::cleanPath(QDir(base).absoluteFilePath(path));
  if (!QFileInfo::exists(absolutePath)) {
    emit errorOccurred(tr("The linked file does not exist"));
    return;
  }

  auto *notebooks = m_services.get<NotebookCoreService>();
  const auto resolved = notebooks->resolvePathToNotebook(absolutePath);
  if (resolved.isEmpty()) {
    if (protectedBuffer) {
      emit errorOccurred(tr("Protected note links can open only indexed notebook notes"));
      return;
    }
    // Keep the ordinary external-file warning, without calling a widget helper.
    confirmExternalLink(QUrl::fromLocalFile(absolutePath));
    return;
  }
  NodeIdentifier nodeId;
  nodeId.notebookId = resolved[QLatin1String(vxcore::kJsonKeyNotebookId)].toString();
  nodeId.relativePath = resolved[QStringLiteral("relativePath")].toString();
  if (protectedBuffer && notebooks->getFileInfo(nodeId.notebookId, nodeId.relativePath).isEmpty()) {
    emit errorOccurred(tr("Protected note links can open only indexed notebook notes"));
    return;
  }
  auto settings = p_settings;
  settings.m_anchor = url.fragment(QUrl::FullyDecoded);
  m_services.get<BufferService>()->openBuffer(nodeId, settings);
}

void MindMapViewWindowController::confirmExternalLink(const QUrl &p_url) {
  m_pendingExternalLink = p_url;
  m_pendingExternalGeneration = m_generation;
  const auto generation = m_generation;
  const QPointer<MindMapViewWindowController> guard(this);
  emit externalLinkConfirmationRequested(p_url);
  // The GUI-thread receiver responds synchronously. Cancel leaves no reusable consent.
  if (guard && generation == m_generation) {
    m_pendingExternalLink = QUrl();
    m_pendingExternalGeneration = 0;
  }
}

void MindMapViewWindowController::openConfirmedExternalLink(const QUrl &p_url) {
  if (m_pendingExternalLink.isEmpty() || m_pendingExternalLink != p_url ||
      m_pendingExternalGeneration != m_generation) {
    return;
  }
  m_pendingExternalLink = QUrl();
  m_pendingExternalGeneration = 0;
  const auto lease = m_buffer.isEncrypted() ? m_buffer.acquireProtectedLease() : nullptr;
  if (!isCurrent(m_generation, lease)) {
    return;
  }
  if (!externalSchemeAllowed(p_url, m_buffer.isEncrypted()) &&
      (m_buffer.isEncrypted() || !p_url.isLocalFile() ||
       !QFileInfo(p_url.toLocalFile()).isAbsolute() || !QFileInfo::exists(p_url.toLocalFile()))) {
    emit errorOccurred(tr("This link is not allowed for this note"));
    return;
  }
  if (!QDesktopServices::openUrl(p_url)) {
    emit errorOccurred(tr("Unable to open the link"));
  }
}
