#ifndef NOTIFICATIONBUTTON2_H
#define NOTIFICATIONBUTTON2_H

#include <QToolButton>

namespace vnotex {

class ServiceLocator;
class NotificationPopup2;
class NotificationMessage;

// Toolbar button for the notification system. Shows a bell icon with a small
// badge counting active (undismissed) messages, auto-shows the popup when a new
// message arrives, and opens the popup listing recent messages on click.
class NotificationButton2 : public QToolButton {
  Q_OBJECT

public:
  NotificationButton2(ServiceLocator &p_services, const QSize &p_iconSize,
                      QWidget *p_parent = nullptr);

  // Refresh the bell icon from the current theme.
  void refreshIcon();

protected:
  void paintEvent(QPaintEvent *p_event) override;

private:
  void updateBadge();

  ServiceLocator &m_services;

  NotificationPopup2 *m_popup = nullptr;

  int m_activeCount = 0;
};

} // namespace vnotex

#endif // NOTIFICATIONBUTTON2_H
