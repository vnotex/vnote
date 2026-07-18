#ifndef ACTIVITYSTICKER_H
#define ACTIVITYSTICKER_H

#include "sticker.h"

#include <QDate>
#include <QVariantMap>
#include <QVector>

#include <core/services/activitystatsservice.h>

class QShowEvent;
class QLabel;
class QToolButton;

namespace vnotex {

class HookManager;
class ActivityChartWidget;
class MetricBadge;

// Dashboard sticker visualizing the per-device activity tracked in activity.db.
// For an active day (today by default, or the Calendar-selected day) it shows a
// row of colorful metric badges and a 30-day trailing active-time chart. Thin
// View: all data comes from ActivityStatsService (MVC rule).
class ActivitySticker : public Sticker {
  Q_OBJECT
public:
  explicit ActivitySticker(ServiceLocator &p_services, QWidget *p_parent = nullptr);
  ~ActivitySticker() override;

  QString typeId() const override;
  QString titleText() const override;

  // Trailing window (days) for the chart, ending at the active day.
  static constexpr int kChartDays = 30;

protected:
  void showEvent(QShowEvent *p_event) override;

private slots:
  void clearDateFilter();

private:
  void onCalendarDateChanged(const QVariantMap &p_args);
  void reloadCurrentMode();
  void updateFilterBar();

  ActivityStatsService *m_statsService = nullptr;

  MetricBadge *m_activeTimeValue = nullptr;
  MetricBadge *m_createdValue = nullptr;
  MetricBadge *m_readValue = nullptr;
  MetricBadge *m_editedValue = nullptr;

  ActivityChartWidget *m_chart = nullptr;

  QWidget *m_filterBar = nullptr;
  QLabel *m_filterLabel = nullptr;
  QToolButton *m_clearButton = nullptr;

  // Invalid = today mode; valid = the Calendar-selected local date.
  QDate m_activeDate;

  HookManager *m_hookManager = nullptr;
  int m_dateHookId = -1;
};

} // namespace vnotex

#endif // ACTIVITYSTICKER_H
