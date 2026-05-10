#ifndef GITEEPROVIDER_H
#define GITEEPROVIDER_H

#include "iimagehostprovider.h"

#include <vtextedit/networkutils.h>

namespace vnotex {

class GiteeProvider : public IImageHostProvider {
  Q_OBJECT
public:
  explicit GiteeProvider(QObject *p_parent = nullptr);

  QString typeId() const Q_DECL_OVERRIDE;

  QString displayName() const Q_DECL_OVERRIDE;

  ImageUploadResult upload(const QByteArray &p_data, const QString &p_path) Q_DECL_OVERRIDE;

  bool supportsDelete() const Q_DECL_OVERRIDE;

  bool remove(const QString &p_url, QString &p_msg) Q_DECL_OVERRIDE;

  bool ownsUrl(const QString &p_url) const Q_DECL_OVERRIDE;

  QJsonObject getConfig() const Q_DECL_OVERRIDE;

  void setConfig(const QJsonObject &p_config) Q_DECL_OVERRIDE;

  bool testConfig(const QJsonObject &p_config, QString &p_msg) Q_DECL_OVERRIDE;

  bool ready() const Q_DECL_OVERRIDE;

private:
  vte::NetworkReply getRepoInfo(const QString &p_token, const QString &p_userName,
                                const QString &p_repoName) const;

  static vte::NetworkAccess::RawHeaderPairs prepareCommonHeaders();

  static QString addAccessToken(const QString &p_token, QString p_url);

  static QString fetchResourcePath(const QString &p_prefix, const QString &p_url);

  static bool isEmptyResponse(const QByteArray &p_data);

  QString m_personalAccessToken;

  QString m_userName;

  QString m_repoName;

  QString m_imageUrlPrefix;

  static const QString c_apiUrl;
};

} // namespace vnotex

#endif // GITEEPROVIDER_H
