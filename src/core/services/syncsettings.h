#ifndef SYNCSETTINGS_H
#define SYNCSETTINGS_H

#include <QMetaType>
#include <QString>

namespace vnotex {

struct SyncCredential {
  QString m_backend;
  QString m_username;
  QString m_secret; // Git PAT or WebDAV password/app password; never configuration.
};

struct SyncSettings {
  QString m_backend = QStringLiteral("git");
  QString m_remoteUrl;
  SyncCredential m_credentials;
};

bool isSupportedSyncBackend(const QString &p_backend);

// Returns an empty string on success, otherwise a translated, redacted message.
QString validateSyncSettings(const SyncSettings &p_settings, bool p_requireCredentials);

// Returns an empty string for an invalid URL or unsupported backend. Git URLs are
// only trimmed; WebDAV URLs are fully encoded with exactly one trailing slash.
QString canonicalSyncRemoteUrl(const SyncSettings &p_settings);

// Returns compact C-API credential JSON, or empty for an unsupported backend.
// The caller must keep the result transient: never persist it in configuration or log it.
QString syncCredentialsJson(const SyncCredential &p_credentials);

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::SyncCredential)

#endif // SYNCSETTINGS_H
