#include "httpapiconfig.h"

#include <QCoreApplication>

#include <cmath>

using namespace vnotex;

HttpApiConfig::HttpApiConfig(IConfigMgr *p_mgr, IConfig *p_parentConfig)
    : IConfig(p_mgr, p_parentConfig),
      m_values{{QStringLiteral("enabled"), false},
               {QStringLiteral("address"), QStringLiteral("127.0.0.1")},
               {QStringLiteral("port"), 18765}} {
  m_sectionName = QStringLiteral("httpApi");
}

void HttpApiConfig::fromJson(const QJsonObject &p_jobj) {
  // SessionConfig supplies absent-field defaults. Missing fields here mean the
  // section had the wrong type: retain a durable rejection, including explicit nulls.
  for (auto it = m_values.begin(); it != m_values.end(); ++it) {
    const auto value = p_jobj.value(it.key());
    it.value() = value.isUndefined() ? QJsonValue(QJsonValue::Null) : value;
  }
}

QJsonObject HttpApiConfig::toJson() const { return m_values; }

bool HttpApiConfig::isEnabled() const { return m_values.value(QStringLiteral("enabled")).toBool(); }

void HttpApiConfig::setEnabled(bool p_enabled) { setValue(QStringLiteral("enabled"), p_enabled); }

QString HttpApiConfig::getAddress() const {
  return m_values.value(QStringLiteral("address")).toString();
}

void HttpApiConfig::setAddress(const QString &p_address) {
  setValue(QStringLiteral("address"), p_address);
}

int HttpApiConfig::getPort() const { return m_values.value(QStringLiteral("port")).toInt(); }

void HttpApiConfig::setPort(int p_port) { setValue(QStringLiteral("port"), p_port); }

void HttpApiConfig::setValue(const QString &p_key, const QJsonValue &p_value) {
  if (m_values.value(p_key) != p_value) {
    m_values.insert(p_key, p_value);
    update();
  }
}

bool HttpApiConfig::isValid(QString *p_error) const {
  if (p_error) {
    p_error->clear();
  }
  auto fail = [p_error](const char *p_message) {
    if (p_error) {
      *p_error = QCoreApplication::translate("vnotex::HttpApiConfig", p_message);
    }
    return false;
  };
  if (!m_values.value(QStringLiteral("enabled")).isBool() ||
      !m_values.value(QStringLiteral("address")).isString() ||
      !m_values.value(QStringLiteral("port")).isDouble()) {
    return fail(
        QT_TRANSLATE_NOOP("vnotex::HttpApiConfig",
                          "HTTP API settings contain missing fields or invalid value types."));
  }
  const double port = m_values.value(QStringLiteral("port")).toDouble();
  if (!std::isfinite(port) || port < 1 || port > 65535 || port != static_cast<int>(port)) {
    return fail(QT_TRANSLATE_NOOP("vnotex::HttpApiConfig",
                                  "The HTTP API port must be an integer from 1 to 65535."));
  }
  const QString address = getAddress();
  if (address != QStringLiteral("127.0.0.1") && address != QStringLiteral("::1")) {
    return fail(QT_TRANSLATE_NOOP(
        "vnotex::HttpApiConfig",
        "The HTTP API address must be a literal loopback address, such as 127.0.0.1 or ::1."));
  }
  return true;
}
