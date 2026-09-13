#include "tooltipservice.h"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>
#include <QResource>
#include <QScopeGuard>

#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/servicelocator.h>
#include <core/services/notificationservice.h>

#include <utility>

using namespace vnotex;

ToolTipService::ToolTipService(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

void ToolTipService::setCatalogPathOverrideForTesting(const QString &p_path) {
  m_catalogPathOverride = p_path;
}

bool ToolTipService::showTipIfDue(const QDate &p_today) {
  auto *configMgr = m_services.get<ConfigMgr2>();
  auto *notifications = m_services.get<NotificationService>();
  if (!configMgr || !notifications) {
    qWarning() << "ToolTipService: missing required service";
    return false;
  }
  if (!p_today.isValid()) {
    qWarning() << "ToolTipService: invalid date" << p_today;
    return false;
  }

  auto &coreConfig = configMgr->getCoreConfig();
  if (!coreConfig.isToolTipsEnabled()) {
    return false;
  }
  const auto lastDate = QDate::fromString(coreConfig.getLastToolTipDate(), Qt::ISODate);
  if (lastDate.isValid() && lastDate >= p_today) {
    return false;
  }

  QString catalogPath = m_catalogPathOverride;
  const QString extraRcc(QStringLiteral("app:vnote_extra.rcc"));
  bool rccRegistered = false;
  auto rccCleanup = qScopeGuard([&extraRcc, &rccRegistered] {
    if (rccRegistered) {
      QResource::unregisterResource(extraRcc);
    }
  });
  if (catalogPath.isEmpty()) {
    if (!QResource::registerResource(extraRcc)) {
      qWarning() << "ToolTipService: failed to register resource" << extraRcc;
      return false;
    }
    rccRegistered = true;
    catalogPath = QStringLiteral(":/vnotex/data/extra/tooltips.json");
  }

  QFile file(catalogPath);
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "ToolTipService: failed to read catalog" << catalogPath << file.errorString();
    return false;
  }
  const auto data = file.readAll();
  if (file.error() != QFileDevice::NoError) {
    qWarning() << "ToolTipService: failed to read catalog" << catalogPath << file.errorString();
    return false;
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
    qWarning() << "ToolTipService: invalid catalog" << catalogPath << parseError.errorString();
    return false;
  }
  const auto tips = document.array();
  if (tips.isEmpty()) {
    qWarning() << "ToolTipService: no usable tips" << catalogPath;
    return false;
  }

  const auto locale = coreConfig.getLocaleToUse();
  const int count = tips.size();
  int position = coreConfig.getNextToolTipIndex() % count;
  QString text;
  for (int scanned = 0; scanned < count; ++scanned) {
    const auto item = tips.at(position).toObject();
    text = item.value(locale).toString().trimmed();
    if (text.isEmpty()) {
      text = item.value(QStringLiteral("en_US")).toString().trimmed();
    }
    if (++position == count) {
      position = 0;
    }
    if (!text.isEmpty()) {
      break;
    }
  }
  if (text.isEmpty()) {
    qWarning() << "ToolTipService: no usable tips" << catalogPath;
    return false;
  }

  NotificationMessage message;
  message.m_title = tr("VNote Tip");
  message.m_text = std::move(text);
  message.m_severity = NotificationMessage::Severity::Info;
  message.m_duration = NotificationMessage::Duration::Long;
  message.m_attention = NotificationMessage::Attention::Interrupt;
  message.m_category = QStringLiteral("tooltip");
  const QPointer<ConfigMgr2> guardedConfig(configMgr);
  message.m_actions.push_back({tr("Never show again"),
                               [guardedConfig] {
                                 if (guardedConfig) {
                                   guardedConfig->getCoreConfig().setToolTipsEnabled(false);
                                 }
                               },
                               true});

  // Consume before notify(): synchronous observers must see the day as already used.
  coreConfig.setLastToolTipDate(p_today.toString(Qt::ISODate));
  coreConfig.setNextToolTipIndex(position);
  notifications->notify(std::move(message));
  return true;
}
