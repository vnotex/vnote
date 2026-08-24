#include "webengineprofileservice.h"

#include <QDebug>
#include <QDir>
#include <QWebEngineProfile>

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

WebEngineProfileService::WebEngineProfileService(const QString &p_cacheRoot, QObject *p_parent)
    : QObject(p_parent) {
  // A named profile is non off-the-record, which is what makes the cache settings take effect.
  m_profile = new QWebEngineProfile(QStringLiteral("vnote"), this);

  const auto cachePath = webCachePath(p_cacheRoot);
  const auto storagePath = webStoragePath(p_cacheRoot);

  const bool cacheDirOk = QDir().mkpath(cachePath);
  const bool storageDirOk = QDir().mkpath(storagePath);

  m_profile->setHttpCacheMaximumSize(c_httpCacheMaximumSize);
  if (cacheDirOk) {
    m_profile->setCachePath(cachePath);
    m_profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
  } else {
    qWarning() << "failed to create web cache directory, falling back to memory HTTP cache"
               << cachePath;
    m_profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
  }

  // A named profile always has persistent storage; never leave this unset and never pass a null
  // string, since either leaves Qt free to write localStorage/IndexedDB, visited links and
  // favicon data under its OWN application-data directory. Point it under VNote's directory
  // unconditionally - even when the pre-creation failed - so the state can never escape there.
  if (!storageDirOk) {
    qWarning() << "failed to create web storage directory" << storagePath;
  }
  m_profile->setPersistentStoragePath(storagePath);

  m_profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);

  qInfo() << "web engine profile cache path" << m_profile->cachePath() << "persistent storage path"
          << m_profile->persistentStoragePath();
}

QWebEngineProfile *WebEngineProfileService::profile() const { return m_profile; }
