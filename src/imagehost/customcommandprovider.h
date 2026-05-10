#ifndef CUSTOMCOMMANDPROVIDER_H
#define CUSTOMCOMMANDPROVIDER_H

#include "iimagehostprovider.h"

namespace vnotex {

class CustomCommandProvider : public IImageHostProvider {
  Q_OBJECT
public:
  explicit CustomCommandProvider(QObject *p_parent = nullptr);

  QString typeId() const Q_DECL_OVERRIDE;

  QString displayName() const Q_DECL_OVERRIDE;

  ImageUploadResult upload(const QByteArray &p_data, const QString &p_path) Q_DECL_OVERRIDE;

  bool supportsDelete() const Q_DECL_OVERRIDE;

  bool remove(const QString &p_url, QString &p_msg) Q_DECL_OVERRIDE;

  bool ownsUrl(const QString &p_url) const Q_DECL_OVERRIDE;

  QJsonObject getConfig() const Q_DECL_OVERRIDE;

  QHash<QString, ConfigFieldHint> getConfigFieldHints() const Q_DECL_OVERRIDE;

  void setConfig(const QJsonObject &p_config) Q_DECL_OVERRIDE;

  bool testConfig(const QJsonObject &p_config, QString &p_msg) Q_DECL_OVERRIDE;

  bool ready() const Q_DECL_OVERRIDE;

private:
  QString m_command;
};

} // namespace vnotex

#endif // CUSTOMCOMMANDPROVIDER_H
