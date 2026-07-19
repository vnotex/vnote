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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotificationService)
#include "test_notificationservice.moc"
