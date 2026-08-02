#ifndef NOTIFICATIONTOAST_H
#define NOTIFICATIONTOAST_H

#include <functional>

#include <QFrame>
#include <QPointer>

#include <core/services/notificationservice.h>

class QHBoxLayout;
class QLabel;
class QProgressBar;
class QTimer;
class QVBoxLayout;

namespace vnotex {

class ServiceLocator;

// Transient surface for Attention::Interrupt notifications.
//
// WHY THIS EXISTS: NotificationPopup2 is a QMenu, so popping it takes a mouse +
// keyboard grab. That is acceptable for a popup the user opened, but hostile for
// a background failure arriving while they type. The toast is a plain CHILD
// widget of the main window, so it can never take window activation and can
// never eat keystrokes.
//
// It is also a child rather than a Qt::Tool top-level on purpose: that avoids
// the Windows quirk where a parentless Qt::Tool window is natively unmapped when
// the app deactivates, plus multi-monitor clamping and taskbar overlap. The cost
// is that it is invisible when the main window is minimized or hidden -- which
// is exactly what the fallback sink (the tray balloon) is for.
//
// ROUTING LIVES HERE, not in MainWindow2, so it is unit-testable: the window
// policy arrives through two injected seams instead.
class NotificationToast : public QFrame {
  Q_OBJECT

public:
  explicit NotificationToast(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  // Anchor the bottom-right corner to this widget's geometry instead of the
  // whole parent. MainWindow2 passes its central widget so the toast sits over
  // the editor rather than over a dock. Also watched for Resize/Move, because
  // showing or hiding a dock changes the central widget's geometry WITHOUT
  // resizing the top-level window.
  void setAnchorWidget(QWidget *p_anchor);

  // True when this widget can actually be seen. MainWindow2 supplies
  //   isVisible() && !(windowState() & Qt::WindowMinimized)
  // -- isVisible() alone is NOT enough, because a minimized top-level window is
  // still logically visible.
  void setCanShowInWindow(std::function<bool()> p_predicate);

  // Where an Interrupt goes when canShowInWindow() is false. MainWindow2 routes
  // it to the tray balloon (title + text only; actions and details are dropped,
  // since a balloon cannot carry them).
  void setFallbackSink(std::function<void(const NotificationMessage &)> p_sink);

  // Id currently on screen, or 0. Test seam / MainWindow2 diagnostics.
  quint64 shownId() const;

signals:
  // The user clicked the toast body: open the full notification list. The toast
  // deliberately does NOT reference NotificationPopup2 (which is private to
  // NotificationButton2), so this stays a pure signal.
  void popupRequested();

protected:
  void enterEvent(
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
      QEnterEvent *p_event
#else
      QEvent *p_event
#endif
      ) override;
  void leaveEvent(QEvent *p_event) override;
  void mouseReleaseEvent(QMouseEvent *p_event) override;
  bool eventFilter(QObject *p_watched, QEvent *p_event) override;

private:
  void setupUI();

  // Wire the service signals that drive the routing table (see the .cpp).
  void setupConnections();

  // Show or refresh for @p_msg, honoring canShowInWindow()/fallbackSink().
  // @p_restartTimer is false for an in-place content refresh of the message
  // already on screen -- an update is not a new event and must not extend its
  // welcome.
  void present(const NotificationMessage &p_msg, bool p_restartTimer);

  // Populate the widgets from @p_msg. Rebuilds the action buttons, so nothing
  // captured before this call may be touched afterwards.
  void render(const NotificationMessage &p_msg);

  void startAutoHideTimer(NotificationMessage::Duration p_duration);

  void hideToast();

  void reposition();

  QIcon severityIcon(NotificationMessage::Severity p_severity) const;

  static const char *severityState(NotificationMessage::Severity p_severity);

  ServiceLocator &m_services;

  QLabel *m_iconLabel = nullptr;
  QLabel *m_titleLabel = nullptr;
  QLabel *m_textLabel = nullptr;
  QProgressBar *m_progressBar = nullptr;
  QHBoxLayout *m_actionLayout = nullptr;
  QVBoxLayout *m_mainLayout = nullptr;

  QTimer *m_autoHideTimer = nullptr;

  // Non-zero while a message is on screen.
  quint64 m_shownId = 0;

  // Duration of the message on screen. Hovering restarts this budget in full on
  // leave (see leaveEvent), so only the enum needs to be kept.
  NotificationMessage::Duration m_shownDuration = NotificationMessage::Duration::Short;

  std::function<bool()> m_canShowInWindow;
  std::function<void(const NotificationMessage &)> m_fallbackSink;

  QPointer<QWidget> m_anchor;
};

} // namespace vnotex

#endif // NOTIFICATIONTOAST_H
