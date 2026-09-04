// NotificationToast: transient surface for Attention::Interrupt messages.
//
// The routing rules live INSIDE the toast (not in MainWindow2) precisely so
// they can be asserted here against the real widget rather than against a
// duplicated copy of the policy. The two window-policy inputs arrive through
// injected seams, which is what makes this test possible without a MainWindow2.

#include <QtTest>

#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QToolButton>
#include <QWidget>

#include <core/servicelocator.h>
#include <core/services/notificationservice.h>
#include <gui/services/themeservice.h>
#include <widgets/notificationtoast.h>

using namespace vnotex;

namespace tests {

class TestNotificationToast : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void test_showsOnInterruptAdded();
  void test_closeControlUsesThemedIconAndHidesOnlyTheToast();
  void test_ignoresPassiveAdded();
  void test_interruptUpdateRefreshesButDoesNotRaise();
  void test_replacementRestartsTheTimer();
  void test_updateForAnotherIdIsIgnored();
  void test_passiveUpdateHidesTheShownMessage();
  void test_dismissHidesTheShownMessage();
  void test_removedHidesTheShownMessage();
  void test_clearAllHides();
  void test_renotifyLeavesTheToastShowingTheNewId();
  void test_fallbackSinkWhenWindowCannotShow();
  void test_fallbackRetiresTheCurrentToast();
  void test_bodyClickRequestsPopup();
  void test_actionReResolvesFromCurrentState();
  void test_callbackMayDestroyTheToast();
  void test_dismissedMessageActionIsInert();

private:
  NotificationMessage interrupt(
      const QString &p_text,
      NotificationMessage::Duration p_duration = NotificationMessage::Duration::Persist) const;

  // The host widget is deliberately never shown, so QWidget::isVisible() would
  // be false no matter what the toast does. isHidden() reflects the toast's OWN
  // show/hide state, independent of its ancestors, which is exactly the thing
  // under test.
  bool toastShown() const { return m_toast && !m_toast->isHidden(); }

  // ServiceLocator holds non-owning pointers and has no clear(), so it is
  // rebuilt per test alongside the service it points at.
  ServiceLocator *m_services = nullptr;
  NotificationService *m_notifications = nullptr;
  ThemeService *m_themeService = nullptr;
  QWidget *m_host = nullptr;
  NotificationToast *m_toast = nullptr;
};

NotificationMessage
TestNotificationToast::interrupt(const QString &p_text,
                                 NotificationMessage::Duration p_duration) const {
  NotificationMessage msg;
  msg.m_title = QStringLiteral("Title");
  msg.m_text = p_text;
  msg.m_attention = NotificationMessage::Attention::Interrupt;
  msg.m_duration = p_duration;
  return msg;
}

void TestNotificationToast::init() {
  m_services = new ServiceLocator();
  m_notifications = new NotificationService();
  m_services->registerService<NotificationService>(m_notifications);

  QString pure = QFINDTESTDATA("../../src/data/extra/themes/pure");
  if (pure.isEmpty()) {
    pure = QFINDTESTDATA("src/data/extra/themes/pure");
  }
  QVERIFY2(!pure.isEmpty(), "bundled 'pure' theme not found");

  ThemeServiceConfig themeConfig;
  themeConfig.themeName = QStringLiteral("pure");
  themeConfig.locale = QStringLiteral("en_US");
  themeConfig.appDataPath = QFileInfo(QFileInfo(pure).absolutePath()).absolutePath();
  m_themeService = new ThemeService(themeConfig);
  m_services->registerService<ThemeService>(m_themeService);

  m_host = new QWidget();
  m_host->resize(800, 600);

  m_toast = new NotificationToast(*m_services, m_host);
  // Default seam: the window can always show it.
  m_toast->setCanShowInWindow([]() { return true; });
}

void TestNotificationToast::cleanup() {
  delete m_host;
  m_host = nullptr;
  m_toast = nullptr;

  delete m_themeService;
  m_themeService = nullptr;

  delete m_notifications;
  m_notifications = nullptr;

  delete m_services;
  m_services = nullptr;
}

void TestNotificationToast::test_showsOnInterruptAdded() {
  const quint64 id = m_notifications->notify(interrupt(QStringLiteral("boom")));

  QVERIFY(toastShown());
  QCOMPARE(m_toast->shownId(), id);
}

void TestNotificationToast::test_closeControlUsesThemedIconAndHidesOnlyTheToast() {
  const quint64 id = m_notifications->notify(interrupt(QStringLiteral("boom")));
  QVERIFY(toastShown());

  QToolButton *closeButton = nullptr;
  for (auto *button : m_toast->findChildren<QToolButton *>()) {
    if (button->toolTip() == QObject::tr("Close")) {
      closeButton = button;
      break;
    }
  }

  QVERIFY2(closeButton, "the toast has no Close tool button");
  QVERIFY(closeButton->text().isEmpty());
  QVERIFY(!closeButton->icon().isNull());
  QCOMPARE(closeButton->iconSize(), QSize(16, 16));
  QCOMPARE(closeButton->focusPolicy(), Qt::NoFocus);

  closeButton->click();

  QVERIFY(!toastShown());
  QCOMPARE(m_toast->shownId(), quint64(0));
  QVERIFY(m_notifications->isActive(id));
}

// Safe-by-default: a producer that did not ask for attention gets none.
void TestNotificationToast::test_ignoresPassiveAdded() {
  NotificationMessage msg;
  msg.m_text = QStringLiteral("quiet");
  m_notifications->notify(msg);

  QVERIFY(!toastShown());
  QCOMPARE(m_toast->shownId(), quint64(0));
}

// An update is a content change to a message already on screen -- never a new
// event. It refreshes, but must not re-raise or extend the toast's welcome.
void TestNotificationToast::test_interruptUpdateRefreshesButDoesNotRaise() {
  // Short duration so the "did the timer restart?" question is observable in
  // bounded wall-clock time.
  const quint64 id = m_notifications->notify(
      interrupt(QStringLiteral("first"), NotificationMessage::Duration::Short));
  QVERIFY(toastShown());

  // Burn most of the Short budget, then update. If the update restarted the
  // timer, the toast would still be up well past the original deadline.
  QTest::qWait(2200);
  QVERIFY2(toastShown(), "the toast retired early; the timing assumption is wrong");

  NotificationMessage next =
      interrupt(QStringLiteral("second"), NotificationMessage::Duration::Short);
  QVERIFY(m_notifications->update(id, next));

  QVERIFY(toastShown());
  QCOMPARE(m_toast->shownId(), id);

  bool found = false;
  for (auto *label : m_toast->findChildren<QLabel *>()) {
    if (label->text() == QStringLiteral("second")) {
      found = true;
      break;
    }
  }
  QVERIFY2(found, "the toast did not refresh its text in place");

  // Past the ORIGINAL deadline. A restarted timer would keep it visible here.
  QTest::qWait(1200);
  QVERIFY2(!toastShown(), "an Interrupt update restarted the auto-hide timer");
}

// Replacement IS a new event, so it does restart the budget -- the counterpart
// to the rule above.
void TestNotificationToast::test_replacementRestartsTheTimer() {
  m_notifications->notify(interrupt(QStringLiteral("first"), NotificationMessage::Duration::Short));
  QVERIFY(toastShown());

  QTest::qWait(2200);
  QVERIFY(toastShown());

  // A different message entirely.
  m_notifications->notify(
      interrupt(QStringLiteral("second"), NotificationMessage::Duration::Short));
  QVERIFY(toastShown());

  // Past the FIRST message's deadline but inside the second's.
  QTest::qWait(1200);
  QVERIFY2(toastShown(), "a replacement did not restart the auto-hide timer");
}

void TestNotificationToast::test_updateForAnotherIdIsIgnored() {
  const quint64 shown = m_notifications->notify(interrupt(QStringLiteral("shown")));

  NotificationMessage other;
  other.m_text = QStringLiteral("other");
  const quint64 otherId = m_notifications->notify(other);

  NotificationMessage next = interrupt(QStringLiteral("hijack"));
  QVERIFY(m_notifications->update(otherId, next));

  // Still showing the original message: an Interrupt update for a DIFFERENT id
  // must not take over the surface.
  QCOMPARE(m_toast->shownId(), shown);
}

// The producer downgraded the message (e.g. the update offer became a passive
// "downloading" state). Leaving the interrupting content up would show a stale
// title and a stale action button.
void TestNotificationToast::test_passiveUpdateHidesTheShownMessage() {
  const quint64 id = m_notifications->notify(interrupt(QStringLiteral("offer")));
  QVERIFY(toastShown());

  NotificationMessage passive;
  passive.m_text = QStringLiteral("downloading");
  passive.m_attention = NotificationMessage::Attention::Passive;
  QVERIFY(m_notifications->update(id, passive));

  QVERIFY(!toastShown());
  QCOMPARE(m_toast->shownId(), quint64(0));
}

void TestNotificationToast::test_dismissHidesTheShownMessage() {
  const quint64 id = m_notifications->notify(interrupt(QStringLiteral("boom")));
  QVERIFY(toastShown());

  m_notifications->dismiss(id);
  QVERIFY(!toastShown());
}

// A message can leave the store without being dismissed (renotify replacement,
// or retention eviction). Holding it on screen would leave inert buttons.
void TestNotificationToast::test_removedHidesTheShownMessage() {
  NotificationMessage msg = interrupt(QStringLiteral("boom"));
  msg.m_dedupKey = QStringLiteral("k");
  m_notifications->notify(msg);
  QVERIFY(toastShown());

  // renotify() with a PASSIVE replacement removes the old generation and adds a
  // message the toast must ignore, so the net effect is a hide.
  NotificationMessage passive;
  passive.m_dedupKey = QStringLiteral("k");
  passive.m_attention = NotificationMessage::Attention::Passive;
  m_notifications->renotify(passive);

  QVERIFY(!toastShown());
}

void TestNotificationToast::test_clearAllHides() {
  m_notifications->notify(interrupt(QStringLiteral("boom")));
  QVERIFY(toastShown());

  m_notifications->clearAll();
  QVERIFY(!toastShown());
}

// renotify() ALWAYS changes the id, so it necessarily runs
// messageRemoved(old) -> hide, then messageAdded(new) -> show. Both deliveries
// are synchronous, so the pair must settle VISIBLE on the new id -- never
// hidden. This is the regression that a deferred/animated hide would introduce.
void TestNotificationToast::test_renotifyLeavesTheToastShowingTheNewId() {
  NotificationMessage first = interrupt(QStringLiteral("first"));
  first.m_dedupKey = QStringLiteral("incident");
  const quint64 oldId = m_notifications->notify(first);
  QVERIFY(toastShown());
  QCOMPARE(m_toast->shownId(), oldId);

  NotificationMessage second = interrupt(QStringLiteral("second"));
  second.m_dedupKey = QStringLiteral("incident");
  const quint64 newId = m_notifications->renotify(second);

  QVERIFY(newId != oldId);
  QVERIFY2(toastShown(), "renotify left the toast hidden");
  QCOMPARE(m_toast->shownId(), newId);
}

// A minimized or hidden main window cannot show an in-window child widget, so
// the message must go to the fallback (the tray balloon in production).
void TestNotificationToast::test_fallbackSinkWhenWindowCannotShow() {
  m_toast->setCanShowInWindow([]() { return false; });

  QVector<QString> sunk;
  m_toast->setFallbackSink(
      [&sunk](const NotificationMessage &p_msg) { sunk.append(p_msg.m_text); });

  m_notifications->notify(interrupt(QStringLiteral("offscreen")));

  QCOMPARE(sunk.size(), 1);
  QCOMPARE(sunk.at(0), QStringLiteral("offscreen"));
  QVERIFY2(!toastShown(), "the toast showed itself on a surface the user cannot see");
  QCOMPARE(m_toast->shownId(), quint64(0));
}

void TestNotificationToast::test_bodyClickRequestsPopup() {
  m_notifications->notify(interrupt(QStringLiteral("boom")));
  QVERIFY(toastShown());

  QSignalSpy spy(m_toast, &NotificationToast::popupRequested);
  QTest::mouseClick(m_toast, Qt::LeftButton);

  QCOMPARE(spy.count(), 1);
  QVERIFY(!toastShown());
}

// The callback AND its dismiss policy must come from the CURRENT service state
// in one lookup, because an Update/Retry callback synchronously replaces the
// action vector. Mutating the stored action behind the rendered button's back
// is what distinguishes re-resolution from a render-time capture.
void TestNotificationToast::test_actionReResolvesFromCurrentState() {
  int staleFired = 0;
  int freshFired = 0;

  NotificationMessage msg = interrupt(QStringLiteral("act"));
  NotificationAction stale;
  stale.m_label = QStringLiteral("Do it");
  stale.m_dismissOnTrigger = false;
  stale.m_callback = [&staleFired]() { ++staleFired; };
  msg.m_actions.append(stale);

  const quint64 id = m_notifications->notify(msg);
  QVERIFY(toastShown());

  // Grab the button as rendered, then swap the stored action underneath it.
  QPushButton *actionBtn = nullptr;
  for (auto *btn : m_toast->findChildren<QPushButton *>()) {
    if (btn->text() == QStringLiteral("Do it")) {
      actionBtn = btn;
      break;
    }
  }
  QVERIFY(actionBtn);

  NotificationMessage replaced = interrupt(QStringLiteral("act"));
  NotificationAction fresh;
  fresh.m_label = QStringLiteral("Do it");
  fresh.m_dismissOnTrigger = false;
  fresh.m_callback = [&freshFired]() { ++freshFired; };
  replaced.m_actions.append(fresh);
  QVERIFY(m_notifications->update(id, replaced));

  // The re-render may have produced a new button; click whichever is live.
  QPushButton *live = nullptr;
  for (auto *btn : m_toast->findChildren<QPushButton *>()) {
    if (btn->text() == QStringLiteral("Do it")) {
      live = btn;
      break;
    }
  }
  QVERIFY(live);
  live->click();

  QVERIFY2(staleFired == 0, "the button invoked the callback captured at render time");
  QCOMPARE(freshFired, 1);
  // m_dismissOnTrigger == false: the producer keeps updating the message.
  QVERIFY(m_notifications->isActive(id));
}

// A callback may synchronously destroy the toast (production: restarting the
// app). Every post-callback access is QPointer-guarded, so this must not crash.
void TestNotificationToast::test_callbackMayDestroyTheToast() {
  NotificationMessage msg = interrupt(QStringLiteral("act"));
  NotificationAction action;
  action.m_label = QStringLiteral("Self destruct");
  action.m_callback = [this]() {
    delete m_toast;
    m_toast = nullptr;
  };
  msg.m_actions.append(action);

  m_notifications->notify(msg);
  QVERIFY(toastShown());

  QPushButton *btn = nullptr;
  for (auto *b : m_toast->findChildren<QPushButton *>()) {
    if (b->text() == QStringLiteral("Self destruct")) {
      btn = b;
      break;
    }
  }
  QVERIFY(btn);
  btn->click();

  QVERIFY(m_toast == nullptr);
}

// A newer interrupt that goes to the tray must retire the older in-window
// toast. A child widget keeps its own shown state while its top-level ancestor
// is minimized, so without this the OLD message would pop back up on restore
// even though a NEWER one had already been delivered elsewhere.
void TestNotificationToast::test_fallbackRetiresTheCurrentToast() {
  m_notifications->notify(interrupt(QStringLiteral("first")));
  QVERIFY(toastShown());

  bool canShow = false;
  m_toast->setCanShowInWindow([&canShow]() { return canShow; });

  QVector<QString> sunk;
  m_toast->setFallbackSink(
      [&sunk](const NotificationMessage &p_msg) { sunk.append(p_msg.m_text); });

  m_notifications->notify(interrupt(QStringLiteral("second")));

  QCOMPARE(sunk.size(), 1);
  QCOMPARE(sunk.at(0), QStringLiteral("second"));
  QVERIFY2(!toastShown(), "the stale in-window toast survived a tray-routed interrupt");
  QCOMPARE(m_toast->shownId(), quint64(0));
}

void TestNotificationToast::test_dismissedMessageActionIsInert() {
  int fired = 0;

  NotificationMessage msg = interrupt(QStringLiteral("act"));
  NotificationAction action;
  action.m_label = QStringLiteral("Do it");
  action.m_callback = [&fired]() { ++fired; };
  msg.m_actions.append(action);

  const quint64 id = m_notifications->notify(msg);

  // Grab the button while it is still rendered, then retire the message behind
  // its back -- the situation a stale row would be in.
  QPushButton *actionBtn = nullptr;
  for (auto *btn : m_toast->findChildren<QPushButton *>()) {
    if (btn->text() == QStringLiteral("Do it")) {
      actionBtn = btn;
      break;
    }
  }
  QVERIFY(actionBtn);

  m_notifications->dismiss(id);
  actionBtn->click();

  QCOMPARE(fired, 0);
}

} // namespace tests

QTEST_MAIN(tests::TestNotificationToast)
#include "test_notificationtoast.moc"
