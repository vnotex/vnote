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
};

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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotificationService)
#include "test_notificationservice.moc"
