#ifndef NOTIFICATIONSERVICE_H
#define NOTIFICATIONSERVICE_H

#include <functional>

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

#include <core/noncopyable.h>

namespace vnotex {

// A single action attached to a notification message. The callback is held in
// memory for the message lifetime and invoked from the widget layer.
struct NotificationAction {
  QString m_label;
  std::function<void()> m_callback;
};

// Value type describing one notification. Copyable (std::function is copyable).
class NotificationMessage {
public:
  enum class Severity { Info, Success, Warning, Error };

  // Controls only how long the auto-popup is shown, NOT memory retention.
  enum class Duration { Short, Long, Persist };

  quint64 m_id = 0;      // Assigned by the service.
  QDateTime m_timestamp; // Set by the service at notify().
  QString m_title;
  QString m_text;
  Severity m_severity = Severity::Info;
  Duration m_duration = Duration::Short;
  QVector<NotificationAction> m_actions;
  bool m_dismissed = false;
};

// In-memory notification store. Qt-minimal (no Qt Widgets): stores data and
// emits signals only. All presentation lives in the widget layer.
class NotificationService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  explicit NotificationService(QObject *p_parent = nullptr);
  ~NotificationService() override = default;

  // Assign id + timestamp, append, emit messageAdded. Returns the new id.
  quint64 notify(NotificationMessage p_msg);

  // Mark the message with @p_id as dismissed and emit messageDismissed.
  void dismiss(quint64 p_id);

  // Clear all messages and emit messagesCleared.
  void clearAll();

  const QVector<NotificationMessage> &messages() const;

  // Count of non-dismissed messages.
  int activeCount() const;

signals:
  void messageAdded(const NotificationMessage &p_msg);
  void messageDismissed(quint64 p_id);
  void messagesCleared();

private:
  QVector<NotificationMessage> m_messages;

  quint64 m_nextId = 1;
};

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::NotificationMessage)

#endif // NOTIFICATIONSERVICE_H
