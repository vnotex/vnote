#ifndef TOOLTIPSERVICE_H
#define TOOLTIPSERVICE_H

#include <QDate>
#include <QObject>
#include <QString>
#include <QStringList>

class QJsonArray;
class QJsonObject;

namespace vnotex {
class ServiceLocator;

// Shared localized catalog access and one synchronous startup notification attempt.
// Notifications and actions outlive the producer.
class ToolTipService : public QObject {
  Q_OBJECT

public:
  explicit ToolTipService(ServiceLocator &p_services, QObject *p_parent = nullptr);

  bool showTipIfDue(const QDate &p_today = QDate::currentDate());
  void setCatalogPathOverrideForTesting(const QString &p_path);

  // Catalog-only selection, independent of daily popup preferences and progress.
  static QString randomTip(const QString &p_catalogPath = QString());

  static QString selectTipText(const QJsonObject &p_tip, const QStringList &p_uiLanguages);

private:
  static QJsonArray loadCatalog(const QString &p_catalogPath);

  ServiceLocator &m_services;
  QString m_catalogPathOverride;
};
} // namespace vnotex

#endif // TOOLTIPSERVICE_H
