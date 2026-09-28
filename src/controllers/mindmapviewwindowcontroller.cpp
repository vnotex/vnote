#include "mindmapviewwindowcontroller.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

#include <core/fileopensettings.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/notebookcoreservice.h>
#include <gui/utils/imageutils.h>
#include <net/networkutils.h>
#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

namespace {
constexpr qint64 c_imageByteLimit = 32 * 1024 * 1024;
constexpr int c_imageTimeoutMs = 15000;

bool readImageFile(const QString &p_path, QByteArray &p_data) {
  const QFileInfo info(p_path);
  if (!info.isAbsolute() || !info.isFile() || info.size() > c_imageByteLimit) {
    return false;
  }
  QFile input(p_path);
  if (!input.open(QIODevice::ReadOnly) || input.size() > c_imageByteLimit) {
    return false;
  }
  p_data = input.read(qMin(input.size(), c_imageByteLimit));
  return input.error() == QFileDevice::NoError && input.atEnd();
}

bool decodeImageUrl(const QString &p_url, QByteArray &p_data) {
  const auto comma = p_url.indexOf(QLatin1Char(','));
  if (comma < 0 || comma > 256) {
    return false;
  }
  auto header = p_url.left(comma).toLower();
  const bool base64 = header.endsWith(QStringLiteral(";base64"));
  if (base64) {
    header.chop(7);
  }
  if (!header.startsWith(QStringLiteral("data:image/")) || header.contains(QLatin1Char(';')) ||
      header.size() <= 11) {
    return false;
  }
  // Bound encoded input before allocating the percent-decoded/base64 payload.
  const qint64 payloadLimit = base64 ? ((c_imageByteLimit + 2) / 3) * 4 : c_imageByteLimit;
  if (p_url.size() - comma - 1 > payloadLimit * 3) {
    return false;
  }
  auto payload = QByteArray::fromPercentEncoding(p_url.mid(comma + 1).toUtf8());
  if (payload.size() > payloadLimit) {
    payload.fill('\0');
    return false;
  }
  if (base64) {
    auto decoded = QByteArray::fromBase64Encoding(payload, QByteArray::AbortOnBase64DecodingErrors);
    payload.fill('\0');
    if (!decoded || decoded.decoded.size() > c_imageByteLimit) {
      decoded.decoded.fill('\0');
      return false;
    }
    p_data = std::move(decoded.decoded);
  } else {
    p_data = std::move(payload);
  }
  return true;
}

QImage decodeImage(QByteArray &p_data) {
  QByteArray sanitized;
  QByteArray mime;
  const bool valid = ImageUtils::protectedImageData(p_data, sanitized, mime);
  p_data.fill('\0');
  const auto image = valid ? QImage::fromData(sanitized) : QImage();
  sanitized.fill('\0');
  return image;
}

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
    : QObject(p_parent), m_services(p_services), m_imageNetwork(new QNetworkAccessManager(this)) {
  connect(m_services.get<BufferService>()->asQObject(), SIGNAL(protectedLockingChanged(bool)), this,
          SLOT(onProtectedLockingChanged(bool)));
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
  const auto replies = m_imageReplies;
  m_imageReplies.clear();
  for (auto *reply : replies) {
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }
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

  QByteArray bytes;
  bool loaded = false;
  if (p_url.startsWith(QStringLiteral("data:image/"), Qt::CaseInsensitive)) {
    loaded = decodeImageUrl(p_url, bytes);
  } else {
    const QUrl source(p_url, QUrl::StrictMode);
    if (m_buffer.isEncrypted()) {
      // Only the authenticated buffer's contained resource reader may access
      // protected-note assets. In particular, file: and network URLs never get IO.
      if (relativeResourceUrl(source)) {
        const auto path = QDir::cleanPath(
            QDir(m_buffer.nodeId().parentPath()).filePath(source.path(QUrl::FullyDecoded)));
        VxCoreError error = VXCORE_OK;
        bytes = m_buffer.readResource(path, &error);
        loaded = error == VXCORE_OK && bytes.size() <= c_imageByteLimit;
      }
    } else if (source.isValid()) {
      if (source.scheme() == QStringLiteral("http") || source.scheme() == QStringLiteral("https")) {
        requestNetworkImage(source, p_url, p_requestId);
        return;
      }
      if (source.isLocalFile()) {
        loaded = readImageFile(source.toLocalFile(), bytes);
      } else if (relativeResourceUrl(source)) {
        const auto base = m_buffer.getResourceBasePath();
        if (!base.isEmpty()) {
          loaded =
              readImageFile(QDir(base).absoluteFilePath(source.path(QUrl::FullyDecoded)), bytes);
        }
      }
    }
  }

  const auto image = loaded ? decodeImage(bytes) : QImage();
  bytes.fill('\0');
  if (isCurrent(generation, lease)) {
    emit imageReady(p_url, p_requestId, image);
  }
}

void MindMapViewWindowController::requestNetworkImage(const QUrl &p_source, const QString &p_url,
                                                      quint64 p_requestId) {
  const auto generation = m_generation;
  auto request = NetworkUtils::networkRequest(p_source);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setMaximumRedirectsAllowed(5);
  request.setTransferTimeout(c_imageTimeoutMs);
  auto *reply = m_imageNetwork->get(request);
  m_imageReplies.insert(reply);
  reply->setReadBufferSize(64 * 1024);
  struct Download {
    QByteArray bytes;
    bool rejected = false;
  };
  const auto download = std::make_shared<Download>();
  const auto receive = [reply, download] {
    if (download->rejected || reply->error() != QNetworkReply::NoError) {
      return;
    }
    const auto length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    const auto originalLength =
        reply->attribute(QNetworkRequest::OriginalContentLengthAttribute).toLongLong();
    const auto available = reply->bytesAvailable();
    if (length > c_imageByteLimit || originalLength > c_imageByteLimit ||
        available > c_imageByteLimit - download->bytes.size()) {
      download->rejected = true;
      if (!reply->isFinished()) {
        reply->abort();
      }
      return;
    }
    if (available > 0) {
      download->bytes.append(reply->read(available));
    }
  };
  connect(reply, &QNetworkReply::metaDataChanged, this, receive);
  connect(reply, &QNetworkReply::readyRead, this, receive);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, download, receive, generation, url = p_url, p_requestId] {
            receive();
            m_imageReplies.remove(reply);
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool valid = !download->rejected && reply->error() == QNetworkReply::NoError &&
                               status >= 200 && status < 300;
            reply->deleteLater();
            if (!isCurrent(generation, {})) {
              return;
            }
            const auto image = valid ? decodeImage(download->bytes) : QImage();
            emit imageReady(url, p_requestId, image);
          });
  // Transfer timeout bounds inactivity; this timer bounds the whole redirect chain.
  QTimer::singleShot(c_imageTimeoutMs, reply, [reply] {
    if (!reply->isFinished()) {
      reply->abort();
    }
  });
}

QString MindMapViewWindowController::insertImage(const QString &p_filePath, QString *p_error) {
  if (p_error) {
    p_error->clear();
  }
  const auto fail = [p_error](const QString &p_message) {
    if (p_error) {
      *p_error = p_message;
    }
    return QString();
  };
  const auto generation = m_generation;
  const auto lease = m_buffer.isEncrypted() ? m_buffer.acquireProtectedLease() : nullptr;
  if (!isCurrent(generation, lease) || m_buffer.isReadOnly()) {
    return fail(tr("The note is not available for image insertion"));
  }

  QByteArray bytes;
  if (!readImageFile(p_filePath, bytes)) {
    bytes.fill('\0');
    return fail(tr("Unable to read image (maximum size is 32 MiB)"));
  }
  QByteArray sanitized;
  QByteArray mime;
  const bool valid = ImageUtils::protectedImageData(bytes, sanitized, mime);
  bytes.fill('\0');
  if (!valid) {
    sanitized.fill('\0');
    return fail(tr("Unable to embed image: unsupported or invalid image data"));
  }
  if (!isCurrent(generation, lease) || m_buffer.isReadOnly()) {
    sanitized.fill('\0');
    return fail(tr("The note is not available for image insertion"));
  }
  auto encoded = sanitized.toBase64();
  sanitized.fill('\0');
  const auto result = QStringLiteral("data:%1;base64,%2")
                          .arg(QString::fromLatin1(mime), QString::fromLatin1(encoded));
  encoded.fill('\0');
  return result;
}

void MindMapViewWindowController::requestOpenLink(const QString &p_url) {
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
  FileOpenSettings settings;
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
