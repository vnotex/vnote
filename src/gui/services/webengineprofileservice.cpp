#include "webengineprofileservice.h"

#include <QDebug>
#include <QDir>
#include <QWebEngineProfile>
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
#include <QWebEngineProfileBuilder>
#endif

#include <core/services/htmltemplateservice.h>
#include <core/vxpdfscheme.h>

#include "vxpdfschemehandler.h"

using namespace vnotex;

namespace {
// 100 MB cap. 0 would mean "let Chromium decide".
const qint64 c_httpCacheMaximumSize = 100 * 1024 * 1024;
} // namespace

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
