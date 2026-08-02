#ifndef NOTIFICATIONSERVICE_H
#define NOTIFICATIONSERVICE_H

#include <functional>

#include <QDateTime>
#include <QHash>
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

  // When false, triggering the action does NOT dismiss the message, so the
  // producer can keep updating it in place (progress -> success/failure).
  bool m_dismissOnTrigger = true;
};

// Value type describing one notification. Copyable (std::function is copyable).
class NotificationMessage {
public:
  enum class Severity { Info, Success, Warning, Error };

  // Controls only how long the auto-popup is shown, NOT memory retention.
  enum class Duration { Short, Long, Persist };

  // How badly the producer needs to be seen. The producer states intent; the
  // widget layer owns placement (see src/widgets/AGENTS.md).
  //
  //   Passive   -> lands in the list and bumps the badge, never steals focus.
  //   Interrupt -> additionally raised on a transient surface (the toast, or
  //                the tray balloon when the main window cannot show it).
  //
  // Defaults to Passive: a producer that forgets to think about this is quiet,
  // which is the safe direction.
  enum class Attention { Passive, Interrupt };

  quint64 m_id = 0;      // Assigned by the service.
  QDateTime m_timestamp; // Set by the service at notify().
  QString m_title;
  QString m_text;
  Severity m_severity = Severity::Info;
  Duration m_duration = Duration::Short;
  Attention m_attention = Attention::Passive;
  QVector<NotificationAction> m_actions;
  bool m_dismissed = false;

  // Producing subsystem, e.g. "update", "sync", "buffer", "imagehost",
  // "viewarea". Diagnostic/grouping only; not rendered today.
  QString m_category;

  // Names an INCIDENT, not a message. While a message with this key is active,
  // notify() folds new content into it instead of appending. Empty = no dedup.
  // See renotify() for the escalation idiom and the retirement contract.
  QString m_dedupKey;

  // Long-form detail (stack, HTTP body, error explanation). Rendered as a
  // collapsible disclosure in the popup list ONLY -- never in the toast or the
  // tray balloon, both of which must stay small.
  QString m_details;

  // Progress rendering hints for the widget layer.
  //   m_progressPermille < 0            -> no progress bar
  //   m_progressPermille in [0, 1000]   -> determinate bar
  //   m_progressIndeterminate == true   -> busy bar (wins over the permille)
  int m_progressPermille = -1;
  bool m_progressIndeterminate = false;
};

// In-memory notification store. Qt-minimal (no Qt Widgets): stores data and
// emits signals only. All presentation lives in the widget layer.
class NotificationService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  explicit NotificationService(QObject *p_parent = nullptr);
  ~NotificationService() override = default;

  // Upper bound on stored messages. Enforced BEFORE appending in notify(), so
  // the store never exceeds it and a messageAdded always names a message that
  // is actually present.
  static constexpr int c_maxMessages = 200;

  // Append a new message (assigning id + timestamp) and emit messageAdded.
  //
  // DEDUP: when m_dedupKey is non-empty and an ACTIVE message already holds
  // that key, this instead overwrites that message's CONTENT in place,
  // preserves its id/timestamp/dismissed/category/dedupKey, emits
  // messageUpdated (NOT messageAdded), and returns the EXISTING id. A dedup
  // update therefore never interrupts and never moves activeCount().
  //
  // Returns the id of the message that now carries this content.
  quint64 notify(NotificationMessage p_msg);

  // Force a NEW interruption for an ongoing dedup key.
  //
  // This is the ONLY sanctioned way to re-interrupt: the toast is raised solely
  // by messageAdded(Interrupt), so a producer whose key is already live cannot
  // get the user's attention through notify() alone. renotify() REMOVES the old
  // generation outright (emitting messageRemoved) and then posts a fresh one.
  //
  // Removal rather than dismissal, because the old generation is never rendered
  // again (the popup skips dismissed messages) and nothing exposes dismissed
  // history; keeping it would only accumulate dead callbacks, add retention
  // pressure, and leave two same-key generations for dismiss()/eviction to
  // disambiguate.
  //
  // With an empty or unused key this is exactly notify().
  quint64 renotify(NotificationMessage p_msg);

  // Replace the content of an existing message IN PLACE: title, text, severity,
  // duration, attention, actions, details and the progress fields. The id,
  // timestamp, dismissed flag, category and dedup key are PRESERVED -- update()
  // never renumbers, re-stamps, resurrects a dismissed message, changes
  // activeCount(), or moves a key to a different message.
  //
  // It emits messageUpdated ONLY. It can legally target an already-dismissed
  // message, so it must never be a signal the transient surfaces treat as a new
  // event -- otherwise a dismissed message could be pushed back on screen.
  //
  // Returns false (and emits nothing) for an unknown id.
  bool update(quint64 p_id, const NotificationMessage &p_msg);

  // True when a message with this id exists and has not been dismissed.
  bool isActive(quint64 p_id) const;

  // Mark the message with @p_id as dismissed and emit messageDismissed.
  void dismiss(quint64 p_id);

  // Retire an incident: dismiss whichever ACTIVE message currently holds this
  // dedup key, so the next failure of the same kind arrives as a fresh,
  // interrupting messageAdded instead of a silent in-place update.
  //
  // Producers MUST call this at every boundary where the incident genuinely
  // ends (sync succeeded, credentials updated, upload succeeded, ...). A missed
  // retirement makes a recurring failure permanently quiet.
  //
  // Returns true when a message was dismissed.
  bool dismissByDedupKey(const QString &p_key);

  // Clear all messages and emit messagesCleared.
  void clearAll();

  const QVector<NotificationMessage> &messages() const;

  // Count of non-dismissed messages.
  int activeCount() const;

signals:
  void messageAdded(const NotificationMessage &p_msg);
  void messageUpdated(const NotificationMessage &p_msg);
  void messageDismissed(quint64 p_id);

  // The message is GONE from the store (replaced by renotify(), or evicted by
  // the retention cap) -- as opposed to messageDismissed, where it remains.
  // Every renderer must drop any reference it holds to this id.
  void messageRemoved(quint64 p_id);

  void messagesCleared();

private:
  // A message's key may leave m_dedupIndex ONLY while the index still points at
  // that exact message.
  //
  // This matters because a message can outlive its ownership of the key: after
  // notify(K) -> dismiss() -> notify(K), the old (dismissed) message and the new
  // (active) one both carry K, but only the new one owns the index entry. An
  // unconditional remove(K) while retiring the old one would strip the LIVE
  // message's entry, so the next notify(K) would append a second active message
  // and pop an unwanted toast instead of updating in place.
  void eraseDedupIndexIfOwned(const NotificationMessage &p_msg);

  // Drop exactly one EXISTING message to make room, emitting messageRemoved.
  // Preference order: oldest dismissed; else oldest active that is cheap to
  // lose (no actions and not Persist); else oldest active outright. The last
  // tier is the unavoidable fallback of having a hard bound at all.
  void evictOneExistingMessage();

  QVector<NotificationMessage> m_messages;

  // dedupKey -> id of the ACTIVE message currently holding it.
  QHash<QString, quint64> m_dedupIndex;

  quint64 m_nextId = 1;
};

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::NotificationMessage)

#endif // NOTIFICATIONSERVICE_H
