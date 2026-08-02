#ifndef NOTIFICATIONBUTTON2_H
#define NOTIFICATIONBUTTON2_H

#include <QToolButton>

namespace vnotex {

class ServiceLocator;
class NotificationPopup2;

// Toolbar button for the notification system. Shows a bell icon with a small
// badge counting active (undismissed) messages, and opens the popup listing
// recent messages on click.
//
// It deliberately does NOT auto-show the popup on a new message: the popup is a
// QMenu, so popping it takes a mouse + keyboard grab. Transient delivery of an
// Attention::Interrupt message belongs to NotificationToast, which is a plain
// child widget and cannot steal focus.
class NotificationButton2 : public QToolButton {
  Q_OBJECT

public:
  NotificationButton2(ServiceLocator &p_services, const QSize &p_iconSize,
                      QWidget *p_parent = nullptr);

  // Refresh the bell icon from the current theme.
  void refreshIcon();

  // Open the notification list. This is the bridge for
  // NotificationToast::popupRequested: the popup is private to this button, so
  // the toast asks rather than reaching in.
  void showPopup();

  // Test seam (unconditional, per ADR-6). The badge is painted straight from
  // this cached count, so asserting on it is what distinguishes "the button
  // reacted to the signal" from "activeCount() happens to be right" -- the
  // latter would pass even with no connection at all.
  int testBadgeCount() const { return m_activeCount; }

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
