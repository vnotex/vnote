#ifndef NOTIFICATIONPOPUP2_H
#define NOTIFICATIONPOPUP2_H

#include "buttonpopup.h"

#include <core/services/notificationservice.h>

class QToolButton;
class QVBoxLayout;
class QWidget;

namespace vnotex {

class ServiceLocator;
class TitleBar;

// Popup listing recent notification messages (newest first). Rebuilds its rows
// from NotificationService::messages() on every show/signal so it never retains
// dangling callback references after clearAll().
//
// This is the click-to-open notification centre only. It NEVER pops itself: it
// is a QMenu, so showing it takes a mouse + keyboard grab, which is unacceptable
// for a message the user did not ask to see. Transient delivery belongs to
// NotificationToast.
class NotificationPopup2 : public ButtonPopup {
  Q_OBJECT

public:
  NotificationPopup2(ServiceLocator &p_services, QToolButton *p_btn, QWidget *p_parent = nullptr);

  // Rebuild the message rows from the current service state.
  void rebuild();

private:
  void setupUI();

  QIcon severityIcon(NotificationMessage::Severity p_severity) const;

  static const char *severityState(NotificationMessage::Severity p_severity);

  ServiceLocator &m_services;

  QWidget *m_container = nullptr;

  TitleBar *m_titleBar = nullptr;

  QVBoxLayout *m_listLayout = nullptr;

  QWidget *m_emptyLabel = nullptr;
};

} // namespace vnotex

#endif // NOTIFICATIONPOPUP2_H
