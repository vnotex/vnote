#include "webengineprofileservice.h"

#include <QDebug>
#include <QDir>
#include <QIODevice>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QReadWriteLock>
#include <QRegularExpression>
#include <QUuid>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
#include <QWebEngineProfileBuilder>
#endif
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <cstring>

#include <atomic>
#include <utility>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QWebEngineDownloadRequest>
#else
#include <QWebEngineDownloadItem>
#endif

#include <core/services/buffer2.h>
#include <core/services/htmltemplateservice.h>
#include <core/vxpdfscheme.h>
#include <gui/utils/imageutils.h>

#include "vxpdfschemehandler.h"

using namespace vnotex;

namespace {
// 100 MB cap. 0 would mean "let Chromium decide".
const qint64 c_httpCacheMaximumSize = 100 * 1024 * 1024;

constexpr auto c_protectedScheme = "vxnote";

void wipeBytes(QByteArray &p_bytes) {
  volatile char *data = p_bytes.data();
  for (int idx = 0; idx < p_bytes.size(); ++idx) {
    data[idx] = 0;
  }
  p_bytes.clear();
}

struct ByteWiper {
  QByteArray &bytes;
  ~ByteWiper() { wipeBytes(bytes); }
};

bool canonicalProtectedPath(const QString &p_path) {
  static const QRegularExpression c_path(QStringLiteral("^/[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*$"));
  if (!c_path.match(p_path).hasMatch()) {
    return false;
  }
  const auto parts = p_path.split(QLatin1Char('/'));
  for (const auto &part : parts) {
    if (part == QStringLiteral(".") || part == QStringLiteral("..")) {
      return false;
    }
  }
  return true;
}

bool scopedProtectedUrl(const QUrl &p_url, const QString &p_token) {
  return p_url.isValid() && p_url.scheme() == QLatin1String(c_protectedScheme) &&
         p_url.host() == p_token && p_url.userInfo().isEmpty() && p_url.port() == -1 &&
         !p_url.hasQuery() && !p_url.hasFragment() &&
         canonicalProtectedPath(p_url.path(QUrl::FullyEncoded));
}

bool protectedAssetPath(const QString &p_path) {
  const auto prefix = QStringLiteral("/assets/");
  if (!p_path.startsWith(prefix)) {
    return false;
  }
  const auto id = p_path.mid(prefix.size());
  const QUuid uuid(id);
  return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces).toLower() == id;
}

QByteArray protectedResourceMime(const QString &p_path) {
  if (p_path.endsWith(QStringLiteral(".js")))
    return QByteArrayLiteral("text/javascript");
  if (p_path.endsWith(QStringLiteral(".css")))
    return QByteArrayLiteral("text/css");
  if (p_path.endsWith(QStringLiteral(".woff2")))
    return QByteArrayLiteral("font/woff2");
  if (p_path.endsWith(QStringLiteral(".woff")))
    return QByteArrayLiteral("font/woff");
  if (p_path.endsWith(QStringLiteral(".ttf")))
    return QByteArrayLiteral("font/ttf");
  if (p_path.endsWith(QStringLiteral(".otf")))
    return QByteArrayLiteral("font/otf");
  return QByteArray();
}

struct ProtectedPageState {
  Buffer2 buffer;
  QString token;
  QString nonce;
  std::atomic<bool> active{true};
  QReadWriteLock lock;
  QByteArray document;
  QHash<QString, QByteArray> resources;

  ~ProtectedPageState() { wipeBytes(document); }
};

class ProtectedReplyBuffer final : public QIODevice {
public:
  ProtectedReplyBuffer(std::shared_ptr<ProtectedPageState> p_state, QByteArray p_bytes,
                       std::shared_ptr<ProtectedBufferLease> p_lease, QObject *p_parent)
      : QIODevice(p_parent), m_state(std::move(p_state)), m_lease(std::move(p_lease)),
        m_bytes(std::move(p_bytes)) {
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
  }
  ~ProtectedReplyBuffer() override { revoke(); }

  void revoke() {
    // Chromium reads this device on another thread. Do not close/seek or
    // mutate QIODevice's read state until the owning job has been destroyed.
    QMutexLocker lock(&m_mutex);
    m_revoked = true;
    wipeBytes(m_bytes);
    m_lease.reset();
  }

  bool isSequential() const override { return false; }
  bool seek(qint64 p_position) override {
    {
      QMutexLocker lock(&m_mutex);
      if (m_revoked || p_position < 0 || p_position > m_bytes.size())
        return false;
      m_offset = p_position;
    }
    return QIODevice::seek(p_position);
  }
  qint64 pos() const override {
    QMutexLocker lock(&m_mutex);
    return m_offset;
  }
  qint64 size() const override {
    QMutexLocker lock(&m_mutex);
    return m_bytes.size();
  }
  qint64 bytesAvailable() const override {
    QMutexLocker lock(&m_mutex);
    return m_revoked ? 0 : m_bytes.size() - m_offset;
  }

protected:
  qint64 readData(char *p_data, qint64 p_size) override {
    QMutexLocker lock(&m_mutex);
    if (m_revoked || !m_state->active.load(std::memory_order_acquire) ||
        (m_lease && !m_lease->isCurrent())) {
      return -1;
    }
    const qint64 count = qMin(p_size, qint64(m_bytes.size()) - m_offset);
    if (count > 0) {
      std::memcpy(p_data, m_bytes.constData() + m_offset, size_t(count));
      m_offset += count;
    }
    return count;
  }
  qint64 writeData(const char *, qint64) override { return -1; }

private:
  mutable QMutex m_mutex;
  std::shared_ptr<ProtectedPageState> m_state;
  std::shared_ptr<ProtectedBufferLease> m_lease;
  QByteArray m_bytes;
  qint64 m_offset = 0;
  bool m_revoked = false;
};

class ProtectedSchemeHandler final : public QWebEngineUrlSchemeHandler {
public:
  ProtectedSchemeHandler(std::shared_ptr<ProtectedPageState> p_state, QObject *p_parent)
      : QWebEngineUrlSchemeHandler(p_parent), m_state(std::move(p_state)) {}
  ~ProtectedSchemeHandler() override { revoke(); }

  void revoke() {
    m_state->active.store(false, std::memory_order_release);
    {
      QWriteLocker lock(&m_state->lock);
      wipeBytes(m_state->document);
      m_state->resources.clear();
    }
    // Jobs can outlive the page. Close their devices and release leases now,
    // rather than making Lock All wait for Chromium to collect an idle job.
    for (const auto &reply : m_replies) {
      if (reply)
        reply->revoke();
    }
    m_replies.clear();
  }

  bool setDocument(const QString &p_html, const QHash<QString, QByteArray> &p_resources) {
    if (!m_state->active.load(std::memory_order_acquire) ||
        !m_state->buffer.acquireProtectedLease() || p_html.isEmpty() ||
        !p_html.contains(
            HtmlTemplateService::protectedContentSecurityPolicy(m_state->nonce).toHtmlEscaped())) {
      return false;
    }
    for (auto it = p_resources.cbegin(); it != p_resources.cend(); ++it) {
      if (!it.key().startsWith(QStringLiteral("/app/")) || !canonicalProtectedPath(it.key()) ||
          protectedResourceMime(it.key()).isEmpty() || it.value().isEmpty()) {
        return false;
      }
    }
    QWriteLocker lock(&m_state->lock);
    // A profile is a single document generation. A new generation gets a new
    // token, profile and nonce rather than republishing beneath pending jobs.
    if (!m_state->document.isEmpty()) {
      return false;
    }
    m_state->document = p_html.toUtf8();
    m_state->resources = p_resources;
    return true;
  }

  void requestStarted(QWebEngineUrlRequestJob *p_job) override {
    const auto url = p_job->requestUrl();
    auto lease = m_state->active.load(std::memory_order_acquire)
                     ? m_state->buffer.acquireProtectedLease()
                     : nullptr;
    if (!lease || p_job->requestMethod() != QByteArrayLiteral("GET") ||
        !scopedProtectedUrl(url, m_state->token)) {
      p_job->fail(QWebEngineUrlRequestJob::RequestDenied);
      return;
    }
    const auto path = url.path();
    QByteArray bytes;
    QByteArray mime;
    const bool asset = protectedAssetPath(path);
    if (asset) {
      VxCoreError error = VXCORE_OK;
      auto input = m_state->buffer.readResource(QStringLiteral("vxasset:") + path.mid(8), &error);
      const bool passive = error == VXCORE_OK && ImageUtils::protectedImageData(input, bytes, mime);
      wipeBytes(input);
      if (!passive || !lease->isCurrent() || !m_state->active.load(std::memory_order_acquire)) {
        wipeBytes(bytes);
        p_job->fail(QWebEngineUrlRequestJob::RequestDenied);
        return;
      }
    } else {
      QReadLocker lock(&m_state->lock);
      if (path == QStringLiteral("/index.html")) {
        bytes = QByteArray(m_state->document.constData(), m_state->document.size());
        mime = QByteArrayLiteral("text/html");
      } else {
        const auto resource = m_state->resources.constFind(path);
        if (resource != m_state->resources.cend()) {
          bytes = resource.value();
          mime = protectedResourceMime(path);
        }
      }
    }
    if (bytes.isEmpty() || mime.isEmpty()) {
      p_job->fail(QWebEngineUrlRequestJob::UrlNotFound);
      return;
    }
    // Only an authenticated asset reply retains a key lease. Page/application
    // resources keep no key alive after this synchronous dispatch returns.
    auto *device = new ProtectedReplyBuffer(m_state, std::move(bytes),
                                            asset ? std::move(lease) : nullptr, p_job);
    m_replies.append(device);
    connect(device, &QObject::destroyed, this, [this, device]() {
      for (int idx = m_replies.size() - 1; idx >= 0; --idx) {
        if (!m_replies.at(idx) || m_replies.at(idx).data() == device) {
          m_replies.removeAt(idx);
        }
      }
    });
    p_job->reply(mime, device);
  }

private:
  std::shared_ptr<ProtectedPageState> m_state;
  QList<QPointer<ProtectedReplyBuffer>> m_replies;
};

class ProtectedRequestInterceptor final : public QWebEngineUrlRequestInterceptor {
public:
  ProtectedRequestInterceptor(std::shared_ptr<ProtectedPageState> p_state, QObject *p_parent)
      : QWebEngineUrlRequestInterceptor(p_parent), m_state(std::move(p_state)) {}

  void interceptRequest(QWebEngineUrlRequestInfo &p_info) override {
    p_info.block(!allowed(p_info));
  }

private:
  bool allowed(const QWebEngineUrlRequestInfo &p_info) const {
    using Info = QWebEngineUrlRequestInfo;
    if (!m_state->active.load(std::memory_order_acquire) ||
        p_info.requestMethod() != QByteArrayLiteral("GET") ||
        !m_state->buffer.acquireProtectedLease()) {
      return false;
    }
    const auto url = p_info.requestUrl();
    const auto type = p_info.resourceType();
    const auto firstParty = p_info.firstPartyUrl().adjusted(QUrl::RemoveFragment);
    const bool scopedParty = scopedProtectedUrl(firstParty, m_state->token) &&
                             firstParty.path() == QStringLiteral("/index.html");
    if (type == Info::ResourceTypeMainFrame) {
      return scopedProtectedUrl(url, m_state->token) &&
             url.path() == QStringLiteral("/index.html") && (firstParty.isEmpty() || scopedParty);
    }
    if (!scopedParty) {
      return false;
    }
    if (url == QUrl(QStringLiteral("qrc:///qtwebchannel/qwebchannel.js"))) {
      return type == Info::ResourceTypeScript;
    }
    if (url.scheme() == QStringLiteral("data")) {
      if (type != Info::ResourceTypeImage || url.hasFragment() || url.hasQuery()) {
        return false;
      }
      auto encoded = url.toEncoded();
      ByteWiper encodedWiper{encoded};
      const int comma = encoded.indexOf(',');
      if (comma < 0 || encoded.size() > 48 * 1024 * 1024 ||
          !encoded.left(comma).endsWith(";base64")) {
        return false;
      }
      const auto mime = encoded.left(comma);
      if (mime != "data:image/png;base64" && mime != "data:image/jpeg;base64" &&
          mime != "data:image/gif;base64" && mime != "data:image/webp;base64" &&
          mime != "data:image/bmp;base64") {
        return false;
      }
      encoded.remove(0, comma + 1);
      auto input = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
      QByteArray output;
      QByteArray actualMime;
      const bool valid = ImageUtils::protectedImageData(input, output, actualMime) &&
                         QByteArrayLiteral("data:") + actualMime + ";base64" == mime;
      wipeBytes(input);
      wipeBytes(output);
      return valid;
    }
    if (!scopedProtectedUrl(url, m_state->token)) {
      return false;
    }
    const auto path = url.path();
    if (protectedAssetPath(path)) {
      return type == Info::ResourceTypeImage;
    }
    QReadLocker lock(&m_state->lock);
    if (!m_state->resources.contains(path)) {
      return false;
    }
    const auto mime = protectedResourceMime(path);
    return (type == Info::ResourceTypeScript && mime == "text/javascript") ||
           (type == Info::ResourceTypeStylesheet && mime == "text/css") ||
           (type == Info::ResourceTypeFontResource && mime.startsWith("font/"));
    // Frames, objects, media, XHR, workers, service workers, WebSocket, pings,
    // prefetch and every unknown request type are denied by this allowlist.
  }

  std::shared_ptr<ProtectedPageState> m_state;
};

} // namespace

struct WebEngineProfileService::ProtectedProfiles {
  QHash<QWebEngineProfile *, ProtectedSchemeHandler *> handlers;
};

WebEngineProfileService::~WebEngineProfileService() {
  if (m_protectedProfiles) {
    // A caller-owned profile may outlive this service. Revoke its token while
    // the buffer service is still alive; the inert state remains profile-owned.
    for (auto *handler : m_protectedProfiles->handlers) {
      handler->revoke();
    }
  }
}

void WebEngineProfileService::registerProtectedScheme() {
  QWebEngineUrlScheme scheme(QByteArrayLiteral("vxnote"));
  scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
  scheme.setDefaultPort(QWebEngineUrlScheme::PortUnspecified);
  // No LocalAccessAllowed, ServiceWorkersAllowed, or ContentSecurityPolicyIgnored.
  scheme.setFlags(QWebEngineUrlScheme::SecureScheme);
  QWebEngineUrlScheme::registerScheme(scheme);
}

WebEngineProfileService::ProtectedPage
WebEngineProfileService::createProtectedProfile(const Buffer2 &p_buffer, QObject *p_parent) {
  ProtectedPage result;
  if (!p_buffer.isEncrypted() || !p_buffer.acquireProtectedLease()) {
    return result;
  }
  auto state = std::make_shared<ProtectedPageState>();
  state->buffer = p_buffer;
  state->token = QUuid::createUuid().toString(QUuid::Id128).toLower();
  state->nonce = QUuid::createUuid().toString(QUuid::Id128).toLower();
  // No storage name: Qt's off-the-record profile never persists its storage.
  auto *profile = new QWebEngineProfile(p_parent ? p_parent : this);
  profile->setHttpCacheType(QWebEngineProfile::NoCache);
  profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
  profile->cookieStore()->setCookieFilter(
      [](const QWebEngineCookieStore::FilterRequest &) { return false; });
  auto *settings = profile->settings();
  settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
  settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
  settings->setAttribute(QWebEngineSettings::LocalStorageEnabled, false);
  settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);
  settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
  settings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
  settings->setAttribute(QWebEngineSettings::HyperlinkAuditingEnabled, false);
  settings->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, false);
  settings->setUnknownUrlSchemePolicy(QWebEngineSettings::DisallowUnknownUrlSchemes);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  connect(profile, &QWebEngineProfile::downloadRequested, profile,
          [](QWebEngineDownloadRequest *p_download) { p_download->cancel(); });
#else
  connect(profile, &QWebEngineProfile::downloadRequested, profile,
          [](QWebEngineDownloadItem *p_download) { p_download->cancel(); });
#endif
  auto *handler = new ProtectedSchemeHandler(state, profile);
  profile->installUrlSchemeHandler(QByteArrayLiteral("vxnote"), handler);
  profile->setUrlRequestInterceptor(new ProtectedRequestInterceptor(state, profile));
  if (!m_protectedProfiles) {
    m_protectedProfiles.reset(new ProtectedProfiles());
  }
  m_protectedProfiles->handlers.insert(profile, handler);
  connect(profile, &QObject::destroyed, this,
          [this, profile]() { m_protectedProfiles->handlers.remove(profile); });
  result.profile = profile;
  result.token = state->token;
  result.nonce = state->nonce;
  result.url = QUrl(QStringLiteral("vxnote://") + state->token + QStringLiteral("/index.html"));
  return result;
}

bool WebEngineProfileService::setProtectedDocument(QWebEngineProfile *p_profile,
                                                   const QString &p_html,
                                                   const QHash<QString, QByteArray> &p_resources) {
  if (!m_protectedProfiles) {
    return false;
  }
  auto *handler = m_protectedProfiles->handlers.value(p_profile);
  return handler && handler->setDocument(p_html, p_resources);
}

void WebEngineProfileService::revokeProtectedProfile(QWebEngineProfile *p_profile) {
  if (m_protectedProfiles) {
    if (auto *handler = m_protectedProfiles->handlers.value(p_profile)) {
      handler->revoke();
    }
  }
}

QString WebEngineProfileService::webCachePath(const QString &p_root) {
  return QDir(p_root).filePath(QStringLiteral("webcache"));
}

QString WebEngineProfileService::webStoragePath(const QString &p_root) {
  return QDir(p_root).filePath(QStringLiteral("webstorage"));
}

WebEngineProfileService::WebEngineProfileService(const QString &p_cacheRoot,
                                                 ConfigMgr2 *p_configMgr,
                                                 HtmlTemplateService *p_templateService,
                                                 QObject *p_parent)
    : QObject(p_parent) {
  const auto cachePath = webCachePath(p_cacheRoot);
  const auto storagePath = webStoragePath(p_cacheRoot);

  const bool cacheDirOk = QDir().mkpath(cachePath);
  const bool storageDirOk = QDir().mkpath(storagePath);

  if (!cacheDirOk) {
    qWarning() << "failed to create web cache directory, falling back to memory HTTP cache"
               << cachePath;
  }
  if (!storageDirOk) {
    qWarning() << "failed to create web storage directory" << storagePath;
  }

  const auto cacheType =
      cacheDirOk ? QWebEngineProfile::DiskHttpCache : QWebEngineProfile::MemoryHttpCache;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  // Qt initializes storage during profile construction. Setting paths afterward
  // still creates its default AppData directory, even if no page ever uses it.
  QWebEngineProfileBuilder builder;
  builder.setCachePath(cachePath);
  builder.setPersistentStoragePath(storagePath);
  builder.setHttpCacheType(cacheType);
  builder.setHttpCacheMaximumSize(c_httpCacheMaximumSize);
  builder.setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
  m_profile = builder.createProfile(QStringLiteral("vnote"), this);
  if (!m_profile) {
    qFatal("failed to create web engine profile: persistent storage path is already in use");
  }
#else
  // Older Qt has no builder; configure the named profile before creating pages.
  m_profile = new QWebEngineProfile(QStringLiteral("vnote"), this);
  m_profile->setCachePath(cachePath);
  m_profile->setPersistentStoragePath(storagePath);
  m_profile->setHttpCacheType(cacheType);
  m_profile->setHttpCacheMaximumSize(c_httpCacheMaximumSize);
  m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
#endif

  qInfo() << "web engine profile cache path" << m_profile->cachePath() << "persistent storage path"
          << m_profile->persistentStoragePath();

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  if (p_configMgr && p_templateService) {
    m_pdfSchemeHandler = new VxPdfSchemeHandler(
        p_configMgr, [p_templateService]() { return p_templateService->getPdfViewerTemplate(); },
        this);
    m_profile->installUrlSchemeHandler(VxPdfScheme::scheme().toUtf8(), m_pdfSchemeHandler);
    qInfo() << "vxpdf URL scheme handler installed";
  } else {
    qWarning() << "vxpdf URL scheme handler not installed (missing dependencies)";
  }
#else
  Q_UNUSED(p_configMgr);
  Q_UNUSED(p_templateService);
#endif
}

QString WebEngineProfileService::registerPdfDocument(const QString &p_absPath) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  return m_pdfSchemeHandler ? m_pdfSchemeHandler->registerDocument(p_absPath) : QString();
#else
  Q_UNUSED(p_absPath);
  return QString();
#endif
}

void WebEngineProfileService::unregisterPdfDocument(const QString &p_token) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  if (m_pdfSchemeHandler) {
    m_pdfSchemeHandler->unregisterDocument(p_token);
  }
#else
  Q_UNUSED(p_token);
#endif
}

bool WebEngineProfileService::hasPdfDocument(const QString &p_token) const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
  return m_pdfSchemeHandler && m_pdfSchemeHandler->hasDocument(p_token);
#else
  Q_UNUSED(p_token);
  return false;
#endif
}

QWebEngineProfile *WebEngineProfileService::profile() const { return m_profile; }
