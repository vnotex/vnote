#ifndef HTTPAPICONFIG_H
#define HTTPAPICONFIG_H

#include "iconfig.h"

namespace vnotex {

class HttpApiConfig : public IConfig {
public:
  explicit HttpApiConfig(IConfigMgr *p_mgr, IConfig *p_parentConfig = nullptr);

  void fromJson(const QJsonObject &p_jobj) override;
  QJsonObject toJson() const override;

  bool isEnabled() const;
  void setEnabled(bool p_enabled);
  QString getAddress() const;
  void setAddress(const QString &p_address);
  int getPort() const;
  void setPort(int p_port);

  // Validate before using any listener settings, including when enabled is false.
  bool isValid(QString *p_error = nullptr) const;

private:
  void setValue(const QString &p_key, const QJsonValue &p_value);

  // Retain malformed input so unrelated preference writes cannot enable a healed config.
  QJsonObject m_values;
};

} // namespace vnotex

#endif // HTTPAPICONFIG_H
