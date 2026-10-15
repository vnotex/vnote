#include "mindmapimageloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <memory>

#include <gui/utils/imageutils.h>
#include <net/networkutils.h>

using namespace vnotex;

namespace {
constexpr qint64 c_imageByteLimit = 32 * 1024 * 1024;
constexpr int c_imageTimeoutMs = 15000;

bool relativeResourceUrl(const QUrl &p_url) {
  const auto path = p_url.path(QUrl::FullyDecoded);
  return p_url.isValid() && p_url.isRelative() && p_url.authority().isEmpty() && !path.isEmpty() &&
         !path.contains(QChar::Null) && QDir::isRelativePath(path) &&
         !path.startsWith(QLatin1Char('/')) && !path.startsWith(QLatin1Char('\\'));
}
} // namespace

MindMapImageLoader::MindMapImageLoader(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_imageNetwork(new QNetworkAccessManager(this)) {
  Q_UNUSED(p_services);
}

MindMapImageLoader::~MindMapImageLoader() { cancel(); }

void MindMapImageLoader::cancel() {
  ++m_generation;
  const auto replies = m_imageReplies;
  m_imageReplies.clear();
  for (auto *reply : replies) {
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }
}

bool MindMapImageLoader::readImageFile(const QString &p_path, QByteArray &p_data) {
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

bool MindMapImageLoader::decodeImageUrl(const QString &p_url, QByteArray &p_data) {
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
  auto encoded = p_url.mid(comma + 1).toUtf8();
  auto payload = QByteArray::fromPercentEncoding(encoded);
  encoded.fill('\0');
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

QImage MindMapImageLoader::decodeImage(QByteArray &p_data) {
  QByteArray sanitized;
  QByteArray mime;
  const bool valid = ImageUtils::protectedImageData(p_data, sanitized, mime);
  p_data.fill('\0');
  const auto image = valid ? QImage::fromData(sanitized) : QImage();
  sanitized.fill('\0');
  return image;
}

void MindMapImageLoader::requestImage(const QString &p_url, quint64 p_requestId,
                                      const QString &p_resourceBasePath) {
  const auto generation = m_generation;
  QByteArray bytes;
  bool loaded = false;
  if (p_url.startsWith(QStringLiteral("data:image/"), Qt::CaseInsensitive)) {
    loaded = decodeImageUrl(p_url, bytes);
  } else {
    const QUrl source(p_url, QUrl::StrictMode);
    if (source.isValid()) {
      if (source.scheme() == QStringLiteral("http") || source.scheme() == QStringLiteral("https")) {
        requestNetworkImage(source, p_url, p_requestId);
        return;
      }
      if (source.isLocalFile()) {
        loaded = readImageFile(source.toLocalFile(), bytes);
      } else if (relativeResourceUrl(source) && !p_resourceBasePath.isEmpty()) {
        loaded = readImageFile(
            QDir(p_resourceBasePath).absoluteFilePath(source.path(QUrl::FullyDecoded)), bytes);
      }
    }
  }

  const auto image = loaded ? decodeImage(bytes) : QImage();
  bytes.fill('\0');
  if (generation == m_generation) {
    emit imageReady(p_url, p_requestId, image);
  }
}

void MindMapImageLoader::requestNetworkImage(const QUrl &p_source, const QString &p_url,
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
    ~Download() { bytes.fill('\0'); }
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
      const auto previousSize = download->bytes.size();
      download->bytes.resize(previousSize + available);
      const auto received = reply->read(download->bytes.data() + previousSize, available);
      if (received < 0) {
        download->rejected = true;
        download->bytes.fill('\0');
        if (!reply->isFinished()) {
          reply->abort();
        }
        return;
      }
      download->bytes.resize(previousSize + received);
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
            disconnect(reply, nullptr, this, nullptr);
            reply->deleteLater();
            if (generation != m_generation) {
              return;
            }
            const auto image = valid ? decodeImage(download->bytes) : QImage();
            download->bytes.fill('\0');
            emit imageReady(url, p_requestId, image);
          });
  // Transfer timeout bounds inactivity; this timer bounds the whole redirect chain.
  QTimer::singleShot(c_imageTimeoutMs, reply, [reply] {
    if (!reply->isFinished()) {
      reply->abort();
    }
  });
}
