#ifndef TOOLTIPSERVICE_H
#define TOOLTIPSERVICE_H

#include <QDate>
#include <QObject>
#include <QString>

namespace vnotex {
class ServiceLocator;

// One synchronous startup attempt; notifications and actions outlive this producer.
class ToolTipService : public QObject {
  Q_OBJECT

public:
  explicit ToolTipService(ServiceLocator &p_services, QObject *p_parent = nullptr);

  bool showTipIfDue(const QDate &p_today = QDate::currentDate());
  void setCatalogPathOverrideForTesting(const QString &p_path);

private:
  ServiceLocator &m_services;
  QString m_catalogPathOverride;
};
} // namespace vnotex

#endif // TOOLTIPSERVICE_H
