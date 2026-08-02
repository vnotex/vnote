// GUILESS test for NotificationService (in-memory notification store).

#include <QtTest>

#include <QVector>

#include <core/services/notificationservice.h>

using namespace vnotex;

namespace tests {

class TestNotificationService : public QObject {
  Q_OBJECT

private slots:
  void test_notifyAssignsMonotonicIds();
  void test_notifySetsTimestamp();
  void test_activeCountAfterAddDismissClear();
  void test_messageAddedSignal();
  void test_messageDismissedSignal();
  void test_messagesClearedSignal();
  void test_dismissIsIdempotent();
  void test_actionCallbackStored();
  void test_actionDismissOnTriggerDefaultsToTrue();
  void test_updatePreservesIdentityAndReplacesContent();
  void test_updateEmitsMessageUpdated();
  void test_updateUnknownIdReturnsFalse();
  void test_updateNeverUndismissesAndLeavesActiveCount();

  // --- Dedup / incident model ---
  void test_dedupFoldsIntoLiveMessage();
  void test_emptyDedupKeyNeverDedupes();
  void test_dedupPreservesIdentityAndKeyOwnership();
  void test_renotifyRemovesOldGenerationAndPostsNew();
  void test_renotifyWithUnusedKeyIsPlainNotify();
  void test_dismissByDedupKeyRetiresIncident();
  void test_distinctKeysAreIndependentIncidents();
  void test_dismissDropsIndexEntry();
  void test_clearAllClearsIndex();
  void test_updatePreservesCategoryAndDedupKey();
  void test_attentionDefaultsToPassive();

  // --- Index-erasure invariant (the reachable dismissed-old/active-new case) ---
  void test_repeatDismissOfOldGenerationKeepsLiveOwnership();
  void test_evictingOldGenerationKeepsLiveOwnership();
  void test_renotifyVariantOfIndexOwnership();

  // --- Retention ---
  void test_capIsEnforcedBeforeAppend();
  void test_incomingMessageIsNeverEvicted();
  void test_evictionPrefersDismissedThenCheapActive();

private:
  // Append cheap, evictable filler until the store holds exactly @p_target
  // messages. Callers use this to sit the store EXACTLY on the cap, so the next
  // single notify() triggers exactly one eviction and the victim is observable.
  static void fillTo(NotificationService &p_service, int p_target);
};

void TestNotificationService::fillTo(NotificationService &p_service, int p_target) {
  int i = 0;
  while (p_service.messages().size() < p_target) {
    NotificationMessage msg;
    msg.m_title = QStringLiteral("filler %1").arg(i++);
    p_service.notify(msg);
  }
  QCOMPARE(p_service.messages().size(), p_target);
}

void TestNotificationService::test_notifyAssignsMonotonicIds() {
  NotificationService service;

  NotificationMessage m1;
  m1.m_title = QStringLiteral("First");
  const quint64 id1 = service.notify(m1);

  NotificationMessage m2;
  m2.m_title = QStringLiteral("Second");
  const quint64 id2 = service.notify(m2);

  QVERIFY(id1 > 0);
  QVERIFY(id2 > id1);
  QCOMPARE(service.messages().size(), 2);
  QCOMPARE(service.messages().at(0).m_id, id1);
  QCOMPARE(service.messages().at(1).m_id, id2);
}

void TestNotificationService::test_notifySetsTimestamp() {
  NotificationService service;

  NotificationMessage msg;
  QVERIFY(msg.m_timestamp.isNull());
  const quint64 id = service.notify(msg);

  const auto &stored = service.messages().at(0);
  QCOMPARE(stored.m_id, id);
  QVERIFY(stored.m_timestamp.isValid());
}

void TestNotificationService::test_activeCountAfterAddDismissClear() {
  NotificationService service;

  QCOMPARE(service.activeCount(), 0);

  const quint64 id1 = service.notify(NotificationMessage());
  const quint64 id2 = service.notify(NotificationMessage());
  QCOMPARE(service.activeCount(), 2);

  service.dismiss(id1);
  QCOMPARE(service.activeCount(), 1);

  service.dismiss(id2);
  QCOMPARE(service.activeCount(), 0);
  // Dismissed messages remain in the list.
  QCOMPARE(service.messages().size(), 2);

  service.clearAll();
  QCOMPARE(service.activeCount(), 0);
  QCOMPARE(service.messages().size(), 0);
}

void TestNotificationService::test_messageAddedSignal() {
  NotificationService service;

  int count = 0;
  quint64 seenId = 0;
  QString seenTitle;
  connect(&service, &NotificationService::messageAdded, &service,
          [&](const NotificationMessage &p_msg) {
            ++count;
            seenId = p_msg.m_id;
            seenTitle = p_msg.m_title;
          });

  NotificationMessage msg;
  msg.m_title = QStringLiteral("Hello");
  const quint64 id = service.notify(msg);

  QCOMPARE(count, 1);
  QCOMPARE(seenId, id);
  QCOMPARE(seenTitle, QStringLiteral("Hello"));
}

void TestNotificationService::test_messageDismissedSignal() {
  NotificationService service;
  const quint64 id = service.notify(NotificationMessage());

  int count = 0;
  quint64 seenId = 0;
  connect(&service, &NotificationService::messageDismissed, &service, [&](quint64 p_id) {
    ++count;
    seenId = p_id;
  });

  service.dismiss(id);
  QCOMPARE(count, 1);
  QCOMPARE(seenId, id);
}

void TestNotificationService::test_messagesClearedSignal() {
  NotificationService service;
  service.notify(NotificationMessage());

  int count = 0;
  connect(&service, &NotificationService::messagesCleared, &service, [&]() { ++count; });

  service.clearAll();
  QCOMPARE(count, 1);

  // Clearing an empty list does not re-emit.
  service.clearAll();
  QCOMPARE(count, 1);
}

void TestNotificationService::test_dismissIsIdempotent() {
  NotificationService service;
  const quint64 id = service.notify(NotificationMessage());

  int count = 0;
  connect(&service, &NotificationService::messageDismissed, &service, [&](quint64) { ++count; });

  service.dismiss(id);
  service.dismiss(id);
  service.dismiss(9999); // Unknown id.
  QCOMPARE(count, 1);
  QCOMPARE(service.activeCount(), 0);
}

void TestNotificationService::test_actionCallbackStored() {
  NotificationService service;

  int fired = 0;
  NotificationMessage msg;
  NotificationAction action;
  action.m_label = QStringLiteral("Undo");
  action.m_callback = [&fired]() { ++fired; };
  msg.m_actions.append(action);

  service.notify(msg);

  const auto &stored = service.messages().at(0);
  QCOMPARE(stored.m_actions.size(), 1);
  QCOMPARE(stored.m_actions.at(0).m_label, QStringLiteral("Undo"));
  stored.m_actions.at(0).m_callback();
  QCOMPARE(fired, 1);
}

// Actions dismiss the message by default; only a producer that wants to keep
// updating it in place (the updater's Update / Cancel / Retry) opts out.
void TestNotificationService::test_actionDismissOnTriggerDefaultsToTrue() {
  NotificationAction action;
  QVERIFY(action.m_dismissOnTrigger);

  NotificationMessage msg;
  QCOMPARE(msg.m_progressPermille, -1);
  QVERIFY(!msg.m_progressIndeterminate);
}

// update() is the in-place replacement used to turn one notification from an
// offer into progress into a terminal state. Identity must survive it.
void TestNotificationService::test_updatePreservesIdentityAndReplacesContent() {
  NotificationService service;

  NotificationMessage original;
  original.m_title = QStringLiteral("Update Available");
  original.m_text = QStringLiteral("VNote 4.3.1 is available.");
  original.m_severity = NotificationMessage::Severity::Info;
  original.m_duration = NotificationMessage::Duration::Short;
  const quint64 id = service.notify(original);
  const QDateTime stamp = service.messages().at(0).m_timestamp;
  QVERIFY(stamp.isValid());

  NotificationMessage progress;
  // Deliberately carries a foreign id/timestamp: the service must ignore both.
  progress.m_id = 9999;
  progress.m_timestamp = QDateTime::fromMSecsSinceEpoch(0);
  progress.m_title = QStringLiteral("Update");
  progress.m_text = QStringLiteral("Downloading");
  progress.m_severity = NotificationMessage::Severity::Success;
  progress.m_duration = NotificationMessage::Duration::Persist;
  progress.m_progressPermille = 500;
  NotificationAction cancel;
  cancel.m_label = QStringLiteral("Cancel");
  cancel.m_dismissOnTrigger = false;
  progress.m_actions.append(cancel);

  QVERIFY(service.update(id, progress));

  const auto &stored = service.messages().at(0);
  QCOMPARE(stored.m_id, id);
  QCOMPARE(stored.m_timestamp, stamp);
  QCOMPARE(stored.m_title, QStringLiteral("Update"));
  QCOMPARE(stored.m_text, QStringLiteral("Downloading"));
  QCOMPARE(stored.m_severity, NotificationMessage::Severity::Success);
  QCOMPARE(stored.m_duration, NotificationMessage::Duration::Persist);
  QCOMPARE(stored.m_progressPermille, 500);
  QCOMPARE(stored.m_actions.size(), 1);
  QVERIFY(!stored.m_actions.at(0).m_dismissOnTrigger);

  // Still exactly one message: update() never appends.
  QCOMPARE(service.messages().size(), 1);
}

void TestNotificationService::test_updateEmitsMessageUpdated() {
  NotificationService service;
  const quint64 id = service.notify(NotificationMessage());

  int updated = 0;
  int added = 0;
  quint64 seenId = 0;
  QString seenText;
  connect(&service, &NotificationService::messageUpdated, &service,
          [&](const NotificationMessage &p_msg) {
            ++updated;
            seenId = p_msg.m_id;
            seenText = p_msg.m_text;
          });
  connect(&service, &NotificationService::messageAdded, &service,
          [&](const NotificationMessage &) { ++added; });

  NotificationMessage next;
  next.m_text = QStringLiteral("progress");
  QVERIFY(service.update(id, next));

  QCOMPARE(updated, 1);
  QCOMPARE(added, 0);
  QCOMPARE(seenId, id);
  QCOMPARE(seenText, QStringLiteral("progress"));
}

void TestNotificationService::test_updateUnknownIdReturnsFalse() {
  NotificationService service;
  service.notify(NotificationMessage());

  int updated = 0;
  connect(&service, &NotificationService::messageUpdated, &service,
          [&](const NotificationMessage &) { ++updated; });

  QVERIFY(!service.update(9999, NotificationMessage()));
  QCOMPARE(updated, 0);
  QCOMPARE(service.messages().size(), 1);
}

// A dismissed message stays dismissed: update() must never resurrect it, and
// the badge count must not move.
void TestNotificationService::test_updateNeverUndismissesAndLeavesActiveCount() {
  NotificationService service;
  const quint64 id = service.notify(NotificationMessage());
  QVERIFY(service.isActive(id));
  QCOMPARE(service.activeCount(), 1);

  // update() on an active message does not change the count either.
  NotificationMessage next;
  next.m_text = QStringLiteral("still active");
  QVERIFY(service.update(id, next));
  QCOMPARE(service.activeCount(), 1);
  QVERIFY(service.isActive(id));

  service.dismiss(id);
  QVERIFY(!service.isActive(id));
  QCOMPARE(service.activeCount(), 0);

  NotificationMessage revived;
  revived.m_text = QStringLiteral("nice try");
  revived.m_dismissed = false;
  QVERIFY(service.update(id, revived));

  QVERIFY2(!service.isActive(id), "update() resurrected a dismissed message");
  QCOMPARE(service.activeCount(), 0);
  QCOMPARE(service.messages().at(0).m_text, QStringLiteral("nice try"));

  QVERIFY(!service.isActive(9999));
}

// ===========================================================================
// Dedup / incident model
// ===========================================================================

namespace {

NotificationMessage keyed(const QString &p_key, const QString &p_text) {
  NotificationMessage msg;
  msg.m_dedupKey = p_key;
  msg.m_text = p_text;
  return msg;
}

} // namespace

// A dedup key names an INCIDENT, not a message: a repeat folds into the live
// message and emits messageUpdated, so it can never interrupt.
void TestNotificationService::test_dedupFoldsIntoLiveMessage() {
  NotificationService service;

  int added = 0;
  int updated = 0;
  connect(&service, &NotificationService::messageAdded, &service,
          [&](const NotificationMessage &) { ++added; });
  connect(&service, &NotificationService::messageUpdated, &service,
          [&](const NotificationMessage &) { ++updated; });

  const quint64 first = service.notify(keyed(QStringLiteral("sync.auth.nb"), QStringLiteral("a")));
  QCOMPARE(added, 1);
  QCOMPARE(updated, 0);

  const quint64 second = service.notify(keyed(QStringLiteral("sync.auth.nb"), QStringLiteral("b")));
  QCOMPARE(second, first);
  QCOMPARE(added, 1);
  QCOMPARE(updated, 1);

  QCOMPARE(service.messages().size(), 1);
  QCOMPARE(service.messages().at(0).m_text, QStringLiteral("b"));
  // Folding must not move the badge.
  QCOMPARE(service.activeCount(), 1);
}

void TestNotificationService::test_emptyDedupKeyNeverDedupes() {
  NotificationService service;

  int added = 0;
  connect(&service, &NotificationService::messageAdded, &service,
          [&](const NotificationMessage &) { ++added; });

  const quint64 a = service.notify(NotificationMessage());
  const quint64 b = service.notify(NotificationMessage());

  QVERIFY(a != b);
  QCOMPARE(added, 2);
  QCOMPARE(service.messages().size(), 2);
}

void TestNotificationService::test_dedupPreservesIdentityAndKeyOwnership() {
  NotificationService service;

  NotificationMessage first;
  first.m_dedupKey = QStringLiteral("k");
  first.m_category = QStringLiteral("sync");
  first.m_text = QStringLiteral("first");
  const quint64 id = service.notify(first);
  const QDateTime stamp = service.messages().at(0).m_timestamp;

  NotificationMessage second;
  second.m_dedupKey = QStringLiteral("k");
  // Deliberately different: the stored category/key must not be overwritten by
  // a folding call, or the index would stop describing the message.
  second.m_category = QStringLiteral("imposter");
  second.m_text = QStringLiteral("second");
  service.notify(second);

  const auto &stored = service.messages().at(0);
  QCOMPARE(stored.m_id, id);
  QCOMPARE(stored.m_timestamp, stamp);
  QCOMPARE(stored.m_category, QStringLiteral("sync"));
  QCOMPARE(stored.m_dedupKey, QStringLiteral("k"));
  QCOMPARE(stored.m_text, QStringLiteral("second"));
}

// renotify() is the ONLY way to force a new interruption for a live key. It
// REMOVES the old generation rather than dismissing it, so no dismissed
// same-key twin is left behind.
void TestNotificationService::test_renotifyRemovesOldGenerationAndPostsNew() {
  NotificationService service;

  const quint64 oldId = service.notify(keyed(QStringLiteral("k"), QStringLiteral("old")));

  QVector<quint64> removed;
  int added = 0;
  int dismissed = 0;
  connect(&service, &NotificationService::messageRemoved, &service,
          [&](quint64 p_id) { removed.append(p_id); });
  connect(&service, &NotificationService::messageAdded, &service,
          [&](const NotificationMessage &) { ++added; });
  connect(&service, &NotificationService::messageDismissed, &service,
          [&](quint64) { ++dismissed; });

  const quint64 newId = service.renotify(keyed(QStringLiteral("k"), QStringLiteral("new")));

  QVERIFY(newId != oldId);
  QCOMPARE(removed.size(), 1);
  QCOMPARE(removed.at(0), oldId);
  QCOMPARE(added, 1);
  // Removed, NOT dismissed: the distinction is what lets renderers drop the id.
  QCOMPARE(dismissed, 0);

  // The old generation is gone from the store entirely.
  QCOMPARE(service.messages().size(), 1);
  QCOMPARE(service.messages().at(0).m_id, newId);
  QCOMPARE(service.messages().at(0).m_text, QStringLiteral("new"));
}

void TestNotificationService::test_renotifyWithUnusedKeyIsPlainNotify() {
  NotificationService service;

  int removed = 0;
  int added = 0;
  connect(&service, &NotificationService::messageRemoved, &service,
          [&](quint64) { ++removed; });
  connect(&service, &NotificationService::messageAdded, &service,
          [&](const NotificationMessage &) { ++added; });

  service.renotify(keyed(QStringLiteral("fresh"), QStringLiteral("x")));
  service.renotify(NotificationMessage()); // keyless

  QCOMPARE(removed, 0);
  QCOMPARE(added, 2);
}

void TestNotificationService::test_dismissByDedupKeyRetiresIncident() {
  NotificationService service;

  const quint64 id = service.notify(keyed(QStringLiteral("k"), QStringLiteral("a")));

  QVERIFY(!service.dismissByDedupKey(QStringLiteral("unknown")));
  QVERIFY(service.dismissByDedupKey(QStringLiteral("k")));
  QVERIFY(!service.isActive(id));

  // Retiring a second time reports nothing to do.
  QVERIFY(!service.dismissByDedupKey(QStringLiteral("k")));

  // The incident is over, so the next failure of the same kind is NEW news and
  // must arrive as messageAdded (which is the only thing that interrupts).
  int added = 0;
  connect(&service, &NotificationService::messageAdded, &service,
          [&](const NotificationMessage &) { ++added; });

  const quint64 next = service.notify(keyed(QStringLiteral("k"), QStringLiteral("b")));
  QCOMPARE(added, 1);
  QVERIFY(next != id);
  QVERIFY(service.isActive(next));
}

// Two different keys are two different incidents. Retiring or replacing one
// must never touch the other.
//
// This is the property the updater relies on to keep the post-restart apply
// result ("update.result") alive across a new transfer ("update.transfer"):
// a check retires the transfer incident, and the outcome of the update that
// just ran must survive that.
void TestNotificationService::test_distinctKeysAreIndependentIncidents() {
  NotificationService service;

  const quint64 resultId =
      service.notify(keyed(QStringLiteral("update.result"), QStringLiteral("applied")));
  const quint64 transferId =
      service.notify(keyed(QStringLiteral("update.transfer"), QStringLiteral("offer")));
  QVERIFY(resultId != transferId);
  QCOMPARE(service.activeCount(), 2);

  // Replacing the transfer incident leaves the result untouched.
  const quint64 newTransferId =
      service.renotify(keyed(QStringLiteral("update.transfer"), QStringLiteral("failed")));
  QVERIFY(newTransferId != transferId);
  QVERIFY2(service.isActive(resultId), "replacing one incident removed another");

  // Retiring the transfer incident likewise.
  QVERIFY(service.dismissByDedupKey(QStringLiteral("update.transfer")));
  QVERIFY2(service.isActive(resultId), "retiring one incident dismissed another");
  QCOMPARE(service.activeCount(), 1);
}

void TestNotificationService::test_dismissDropsIndexEntry() {
  NotificationService service;

  const quint64 id = service.notify(keyed(QStringLiteral("k"), QStringLiteral("a")));
  service.dismiss(id);

  int added = 0;
  connect(&service, &NotificationService::messageAdded, &service,
          [&](const NotificationMessage &) { ++added; });

  const quint64 next = service.notify(keyed(QStringLiteral("k"), QStringLiteral("b")));
  QVERIFY(next != id);
  QCOMPARE(added, 1);
}

void TestNotificationService::test_clearAllClearsIndex() {
  NotificationService service;

  service.notify(keyed(QStringLiteral("k"), QStringLiteral("a")));
  service.clearAll();

  int added = 0;
  connect(&service, &NotificationService::messageAdded, &service,
          [&](const NotificationMessage &) { ++added; });

  service.notify(keyed(QStringLiteral("k"), QStringLiteral("b")));
  QCOMPARE(added, 1);
  QCOMPARE(service.messages().size(), 1);
}

void TestNotificationService::test_updatePreservesCategoryAndDedupKey() {
  NotificationService service;

  NotificationMessage original;
  original.m_category = QStringLiteral("update");
  original.m_dedupKey = QStringLiteral("update.transfer");
  original.m_attention = NotificationMessage::Attention::Interrupt;
  original.m_details = QStringLiteral("before");
  const quint64 id = service.notify(original);

  NotificationMessage next;
  // Identity fields: must be ignored.
  next.m_category = QStringLiteral("hijack");
  next.m_dedupKey = QStringLiteral("hijack.key");
  // Content fields: must be replaced.
  next.m_attention = NotificationMessage::Attention::Passive;
  next.m_details = QStringLiteral("after");
  QVERIFY(service.update(id, next));

  const auto &stored = service.messages().at(0);
  QCOMPARE(stored.m_category, QStringLiteral("update"));
  QCOMPARE(stored.m_dedupKey, QStringLiteral("update.transfer"));
  QCOMPARE(stored.m_attention, NotificationMessage::Attention::Passive);
  QCOMPARE(stored.m_details, QStringLiteral("after"));

  // The key still resolves to this message, so a later notify() still folds.
  int added = 0;
  connect(&service, &NotificationService::messageAdded, &service,
          [&](const NotificationMessage &) { ++added; });
  QCOMPARE(service.notify(keyed(QStringLiteral("update.transfer"), QStringLiteral("z"))), id);
  QCOMPARE(added, 0);
}

// Safe-by-default: a producer that does not think about attention is quiet.
void TestNotificationService::test_attentionDefaultsToPassive() {
  NotificationMessage msg;
  QCOMPARE(msg.m_attention, NotificationMessage::Attention::Passive);

  NotificationService service;
  service.notify(NotificationMessage());
  QCOMPARE(service.messages().at(0).m_attention, NotificationMessage::Attention::Passive);
}

// ===========================================================================
// Index-erasure invariant
//
// A message can outlive its OWNERSHIP of a dedup key: notify(K) -> dismiss ->
// notify(K) leaves a dismissed old generation and an active new one, both
// carrying K, with only the new one owning the index entry. Retiring the old
// one must NOT strip the live message's entry -- otherwise the next notify(K)
// would append a second active message and pop an unwanted toast.
//
// (A "notify -> renotify -> retire the older one" sequence is NOT constructible:
// renotify() removes the generation it replaces.)
// ===========================================================================

void TestNotificationService::test_repeatDismissOfOldGenerationKeepsLiveOwnership() {
  NotificationService service;

  const quint64 oldId = service.notify(keyed(QStringLiteral("K"), QStringLiteral("old")));
  service.dismiss(oldId);
  const quint64 liveId = service.notify(keyed(QStringLiteral("K"), QStringLiteral("live")));
  QVERIFY(liveId != oldId);

  // Dismissing the already-dismissed old generation must be inert.
  service.dismiss(oldId);

  int added = 0;
  int updated = 0;
  connect(&service, &NotificationService::messageAdded, &service,
          [&](const NotificationMessage &) { ++added; });
  connect(&service, &NotificationService::messageUpdated, &service,
          [&](const NotificationMessage &) { ++updated; });

  QCOMPARE(service.notify(keyed(QStringLiteral("K"), QStringLiteral("next"))), liveId);
  QCOMPARE(added, 0);
  QCOMPARE(updated, 1);
}

void TestNotificationService::test_evictingOldGenerationKeepsLiveOwnership() {
  NotificationService service;

  const quint64 oldId = service.notify(keyed(QStringLiteral("K"), QStringLiteral("old")));
  service.dismiss(oldId);
  const quint64 liveId = service.notify(keyed(QStringLiteral("K"), QStringLiteral("live")));

  // Sit exactly on the cap so the next notify() evicts exactly once. The old
  // generation is the only dismissed message, so it is the chosen victim.
  fillTo(service, NotificationService::c_maxMessages);

  QVector<quint64> removed;
  connect(&service, &NotificationService::messageRemoved, &service,
          [&](quint64 p_id) { removed.append(p_id); });

  service.notify(NotificationMessage());

  QCOMPARE(removed.size(), 1);
  QCOMPARE(removed.at(0), oldId);
  QVERIFY(service.isActive(liveId));

  int added = 0;
  int updated = 0;
  connect(&service, &NotificationService::messageAdded, &service,
          [&](const NotificationMessage &) { ++added; });
  connect(&service, &NotificationService::messageUpdated, &service,
          [&](const NotificationMessage &) { ++updated; });

  QCOMPARE(service.notify(keyed(QStringLiteral("K"), QStringLiteral("next"))), liveId);
  QVERIFY2(added == 0, "eviction stripped the live message's dedup index entry");
  QCOMPARE(updated, 1);
}

void TestNotificationService::test_renotifyVariantOfIndexOwnership() {
  NotificationService service;

  // A: dismissed old generation, still carrying K.
  const quint64 a = service.notify(keyed(QStringLiteral("K"), QStringLiteral("a")));
  service.dismiss(a);
  // B: active, owns K.
  const quint64 b = service.notify(keyed(QStringLiteral("K"), QStringLiteral("b")));
  // C: replaces B (B is removed outright), now owns K.
  const quint64 c = service.renotify(keyed(QStringLiteral("K"), QStringLiteral("c")));
  QVERIFY(c != b);
  QVERIFY(!service.isActive(b));

  fillTo(service, NotificationService::c_maxMessages);

  QVector<quint64> removed;
  connect(&service, &NotificationService::messageRemoved, &service,
          [&](quint64 p_id) { removed.append(p_id); });

  service.notify(NotificationMessage());
  QCOMPARE(removed.size(), 1);
  QCOMPARE(removed.at(0), a);

  int added = 0;
  connect(&service, &NotificationService::messageAdded, &service,
          [&](const NotificationMessage &) { ++added; });
  QCOMPARE(service.notify(keyed(QStringLiteral("K"), QStringLiteral("d"))), c);
  QCOMPARE(added, 0);
}

// ===========================================================================
// Retention
// ===========================================================================

// The loop condition is >=, not >: a store holding exactly c_maxMessages is not
// "over cap", yet appending to it would put it one past.
void TestNotificationService::test_capIsEnforcedBeforeAppend() {
  NotificationService service;

  fillTo(service, NotificationService::c_maxMessages);
  QCOMPARE(service.messages().size(), NotificationService::c_maxMessages);

  for (int i = 0; i < 25; ++i) {
    service.notify(NotificationMessage());
    QVERIFY2(service.messages().size() <= NotificationService::c_maxMessages,
             "the store grew past the cap");
  }
  QCOMPARE(service.messages().size(), NotificationService::c_maxMessages);
}

void TestNotificationService::test_incomingMessageIsNeverEvicted() {
  NotificationService service;
  fillTo(service, NotificationService::c_maxMessages);

  quint64 addedId = 0;
  connect(&service, &NotificationService::messageAdded, &service,
          [&](const NotificationMessage &p_msg) { addedId = p_msg.m_id; });

  NotificationMessage msg;
  msg.m_title = QStringLiteral("incoming");
  const quint64 id = service.notify(msg);

  QCOMPARE(addedId, id);
  // messageAdded must always name a message that is actually stored.
  bool found = false;
  for (const auto &m : service.messages()) {
    if (m.m_id == id) {
      found = true;
      break;
    }
  }
  QVERIFY2(found, "messageAdded named a message that was immediately evicted");
}

void TestNotificationService::test_evictionPrefersDismissedThenCheapActive() {
  NotificationService service;

  // Oldest: an actionable Persist message -- the LAST thing worth losing, since
  // its button may be the user's only recovery affordance.
  NotificationMessage precious;
  precious.m_title = QStringLiteral("precious");
  precious.m_duration = NotificationMessage::Duration::Persist;
  NotificationAction act;
  act.m_label = QStringLiteral("Restart");
  precious.m_actions.append(act);
  const quint64 preciousId = service.notify(precious);

  // Next oldest: a cheap active message (no actions, not Persist).
  NotificationMessage cheap;
  cheap.m_title = QStringLiteral("cheap");
  cheap.m_duration = NotificationMessage::Duration::Long;
  const quint64 cheapId = service.notify(cheap);

  // A dismissed message, newer than both, must still go first.
  const quint64 dismissedId = service.notify(NotificationMessage());
  service.dismiss(dismissedId);

  // Filler is NEWER than all three, so age never rescues `precious`: only the
  // "cheap first" preference can.
  fillTo(service, NotificationService::c_maxMessages);

  QVector<quint64> removed;
  connect(&service, &NotificationService::messageRemoved, &service,
          [&](quint64 p_id) { removed.append(p_id); });

  // Three appends -> exactly three evictions.
  for (int i = 0; i < 3; ++i) {
    service.notify(NotificationMessage());
  }

  QCOMPARE(removed.size(), 3);
  // 1st: the only dismissed message, despite being the newest of the three.
  QCOMPARE(removed.at(0), dismissedId);
  // 2nd: the oldest CHEAP active. `precious` is older but is Persist + has an
  // action, so it is passed over.
  QCOMPARE(removed.at(1), cheapId);
  QVERIFY2(!removed.contains(preciousId),
           "an actionable Persist message was evicted while cheap ones remained");
  QVERIFY(service.isActive(preciousId));
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotificationService)
#include "test_notificationservice.moc"
