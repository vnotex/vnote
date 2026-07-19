#ifndef NOTIFICATIONPOPUP2_H
#define NOTIFICATIONPOPUP2_H

#include "buttonpopup.h"

#include <core/services/notificationservice.h>

class QToolButton;
class QVBoxLayout;
class QWidget;
class QTimer;

namespace vnotex {

class ServiceLocator;
class TitleBar;

// Popup listing recent notification messages (newest first). Rebuilds its rows
// from NotificationService::messages() on every show/signal so it never retains
// dangling callback references after clearAll().
class NotificationPopup2 : public ButtonPopup {
  Q_OBJECT

public:
  NotificationPopup2(ServiceLocator &p_services, QToolButton *p_btn, QWidget *p_parent = nullptr);

  // Rebuild the message rows from the current service state.
  void rebuild();

  // Rebuild, then pop up at the button and arm the auto-hide timer according to
  // @p_msg's Duration (Persist disables the timer). Used for the auto-popup on a
  // newly arrived message.
  void showTransient(const NotificationMessage &p_msg);

private:
  void setupUI();

  QIcon severityIcon(NotificationMessage::Severity p_severity) const;

  ServiceLocator &m_services;

  QWidget *m_container = nullptr;

  TitleBar *m_titleBar = nullptr;

  QVBoxLayout *m_listLayout = nullptr;

  QWidget *m_emptyLabel = nullptr;

  QTimer *m_autoHideTimer = nullptr;
};

} // namespace vnotex

#endif // NOTIFICATIONPOPUP2_H
