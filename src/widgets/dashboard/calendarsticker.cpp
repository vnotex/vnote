#include "calendarsticker.h"

#include <QCalendarWidget>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>

using namespace vnotex;

CalendarSticker::CalendarSticker(ServiceLocator &p_services, QWidget *p_parent)
    : Sticker(p_services, p_parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_calendar = new QCalendarWidget(this);
  m_calendar->setGridVisible(true);
  layout->addWidget(m_calendar);

  // selectionChanged is the single source of truth for date changes: it covers
  // mouse clicks AND keyboard navigation, and fires exactly once per change
  // (unlike clicked/activated which can double-fire or miss keyboard moves).
  connect(m_calendar, &QCalendarWidget::selectionChanged, this,
          &CalendarSticker::onSelectionChanged);
}

QString CalendarSticker::typeId() const { return QStringLiteral("calendar"); }

QString CalendarSticker::titleText() const { return tr("Calendar"); }

QJsonObject CalendarSticker::saveSettings() const {
  QJsonObject obj;
  if (m_calendar) {
    obj[QStringLiteral("selectedDate")] =
        m_calendar->selectedDate().toString(Qt::ISODate);
  }
  return obj;
}

void CalendarSticker::loadSettings(const QJsonObject &p_settings) {
  if (!m_calendar) {
    return;
  }
  const QString dateStr = p_settings.value(QStringLiteral("selectedDate")).toString();
  const QDate date = QDate::fromString(dateStr, Qt::ISODate);
  if (date.isValid()) {
    // Block selectionChanged: restoring persisted state is not a user action, so
    // it must not broadcast the date-changed hook or trigger a re-persist.
    QSignalBlocker blocker(m_calendar);
    m_calendar->setSelectedDate(date);
  }
}

void CalendarSticker::onSelectionChanged() {
  if (!m_calendar) {
    return;
  }
  const QDate date = m_calendar->selectedDate();
  if (!date.isValid()) {
    return;
  }
  broadcastDate(date);
  emit settingsChanged();
}

void CalendarSticker::broadcastDate(const QDate &p_date) {
  auto *hookMgr = m_services.get<HookManager>();
  if (!hookMgr) {
    return;
  }
  QVariantMap args;
  args[QStringLiteral("date")] = p_date.toString(QStringLiteral("yyyy-MM-dd"));
  hookMgr->doAction(HookNames::DashboardCalendarDateChanged, args);
}
