#ifndef CALENDARSTICKER_H
#define CALENDARSTICKER_H

#include "sticker.h"

#include <QDate>

class QCalendarWidget;

namespace vnotex {

// First concrete sticker: hosts a QCalendarWidget. When the user changes the
// selected date, it broadcasts HookNames::DashboardCalendarDateChanged over the
// HookManager so other stickers can react.
class CalendarSticker : public Sticker {
  Q_OBJECT
public:
  explicit CalendarSticker(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString typeId() const override;
  QString titleText() const override;

  QJsonObject saveSettings() const override;
  void loadSettings(const QJsonObject &p_settings) override;

private slots:
  void onSelectionChanged();

private:
  // Fire the calendar-date-changed hook. Deliberately fired from the widget
  // (not a service) as a self-contained exception to the "hooks in services"
  // rule: this is pure inter-sticker communication with no core operation to
  // wrap. See plan / AGENTS.md hook-emission rule.
  void broadcastDate(const QDate &p_date);

  QCalendarWidget *m_calendar = nullptr;
};

} // namespace vnotex

#endif // CALENDARSTICKER_H
