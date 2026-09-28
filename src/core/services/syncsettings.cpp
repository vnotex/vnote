#include "syncsettings.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

#include <sync/sync_json_keys.h>

using namespace vnotex;

namespace {

bool isSafeCollectionPath(const QString &p_encodedPath) {
  const auto segments = p_encodedPath.split(QLatin1Char('/'));
  for (const auto &segment : segments) {
    const auto bytes = QByteArray::fromPercentEncoding(segment.toUtf8());
    const auto decoded = QString::fromUtf8(bytes);
    if (decoded.toUtf8() != bytes || decoded == QLatin1String(".") ||
        decoded == QLatin1String("..")) {
      return false;
    }
    for (const auto character : decoded) {
      if (character == QLatin1Char('/') || character == QLatin1Char('\\') ||
          character.unicode() < 0x20 || character.unicode() == 0x7f) {
        return false;
      }
    }
  }
  return true;
}

} // namespace

bool vnotex::isSupportedSyncBackend(const QString &p_backend) {
  return p_backend == QLatin1String("git") || p_backend == QLatin1String("webdav");
}

QString vnotex::canonicalSyncRemoteUrl(const SyncSettings &p_settings) {
  const auto remoteUrl = p_settings.m_remoteUrl.trimmed();
  if (p_settings.m_backend == QLatin1String("git")) {
    // Preserve the existing remote-open controller/dialog URL contract.
    static const QRegularExpression scheme(QStringLiteral("^(https://|file:///)\\S+$"));
    return scheme.match(remoteUrl).hasMatch() ? remoteUrl : QString();
  }
  if (p_settings.m_backend != QLatin1String("webdav")) {
    return QString();
  }

  QUrl url(remoteUrl, QUrl::StrictMode);
  const auto scheme = url.scheme();
  if (!url.isValid() || url.isRelative() ||
      (scheme != QLatin1String("http") && scheme != QLatin1String("https")) ||
      url.host().isEmpty() || url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@')) ||
      url.hasQuery() || url.hasFragment()) {
    return QString();
  }

  auto path = url.path(QUrl::FullyEncoded);
  if (!isSafeCollectionPath(path)) {
    return QString();
  }
  while (path.endsWith(QLatin1Char('/'))) {
    path.chop(1);
  }
  path.append(QLatin1Char('/'));
  // Keep the path encoded while changing its trailing slash: decoded reserved
  // characters must not acquire URL syntax or get percent-encoded a second time.
  url.setPath(path, QUrl::StrictMode);
  url.setHost(url.host().toLower());
  if ((scheme == QLatin1String("http") && url.port() == 80) ||
      (scheme == QLatin1String("https") && url.port() == 443)) {
    url.setPort(-1);
  }
  return url.toString(QUrl::FullyEncoded);
}

QString vnotex::validateSyncSettings(const SyncSettings &p_settings, bool p_requireCredentials) {
  if (!isSupportedSyncBackend(p_settings.m_backend)) {
    return QCoreApplication::translate("SyncSettings", "Unsupported sync backend.");
  }
  if (p_settings.m_remoteUrl.trimmed().isEmpty()) {
    return QCoreApplication::translate("SyncSettings", "Remote URL must not be empty.");
  }
  if (canonicalSyncRemoteUrl(p_settings).isEmpty()) {
    if (p_settings.m_backend == QLatin1String("git")) {
      return QCoreApplication::translate("SyncSettings",
                                         "Remote URL must use HTTPS or file:// scheme.");
    }
    return QCoreApplication::translate(
        "SyncSettings", "WebDAV requires an absolute HTTP or HTTPS collection URL with a host and "
                        "a safe path, without user information, query, or fragment.");
  }

  const auto &credentials = p_settings.m_credentials;
  const bool hasCredentials = !credentials.m_backend.isEmpty() ||
                              !credentials.m_username.isEmpty() || !credentials.m_secret.isEmpty();
  if (hasCredentials && credentials.m_backend != p_settings.m_backend) {
    return QCoreApplication::translate("SyncSettings",
                                       "Credentials do not match the selected sync backend.");
  }
  if (p_requireCredentials) {
    if (p_settings.m_backend == QLatin1String("git")) {
      if (credentials.m_secret.isEmpty()) {
        return QCoreApplication::translate("SyncSettings", "PAT is required to enable sync.");
      }
    } else {
      if (credentials.m_username.isEmpty()) {
        return QCoreApplication::translate("SyncSettings",
                                           "A username is required to enable WebDAV sync.");
      }
      if (credentials.m_secret.isEmpty()) {
        return QCoreApplication::translate(
            "SyncSettings", "A password or app password is required to enable WebDAV sync.");
      }
    }
  }
  return QString();
}

QString vnotex::syncCredentialsJson(const SyncCredential &p_credentials) {
  QJsonObject object;
  if (p_credentials.m_backend == QLatin1String("git")) {
    object[QLatin1String(vxcore::kJsonKeyPat)] = p_credentials.m_secret;
  } else if (p_credentials.m_backend == QLatin1String("webdav")) {
    QJsonObject extra;
    extra[QLatin1String(vxcore::kJsonKeyUsername)] = p_credentials.m_username;
    extra[QLatin1String(vxcore::kJsonKeyPassword)] = p_credentials.m_secret;
    object[QLatin1String(vxcore::kJsonKeyExtra)] = extra;
  } else {
    return QString();
  }
  return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
