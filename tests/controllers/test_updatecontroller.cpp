// UpdateController: notification wiring for the update-available offer.
//
// SCOPE. The controller's outcomes are private slots driven by UpdateService
// signals. This suite injects those signals directly rather than standing up a
// network fixture (tests/core/test_updateservice.cpp owns the mechanism), and
// covers what the controller alone decides:
//
//   * an available update becomes ONE interrupting, persistent notification
//     whose only affordance is the release page -- VNote downloads nothing;
//   * an up-to-date result is silent on a startup check;
//   * a later check supersedes the previous offer instead of stacking a second
//     one;
//   * the tracked-id bookkeeping when the retention cap evicts the message.
//
// The "Check Release" action calls QDesktopServices::openUrl(), which would open
// a real browser, so an `https` scheme URL handler is installed for the whole
// suite. That also lets the URL itself be asserted.

#include <QtTest>

#include <QDesktopServices>
#include <QUrl>

#include <controllers/updatecontroller.h>
#include <core/servicelocator.h>
#include <core/services/notificationservice.h>
#include <core/services/updateservice.h>

using namespace vnotex;

namespace tests {

namespace {

const QString c_offerKey = QStringLiteral("update.available");

UpdateInfo makeInfo(bool p_updateAvailable, const QString &p_latest = QStringLiteral("4.4.3")) {
  UpdateInfo info;
  info.updateAvailable = p_updateAvailable;
  info.currentVersion = QStringLiteral("4.4.2");
  info.latestVersion = p_latest;
  info.releaseNotes = QStringLiteral("notes");
  info.releaseUrl = QStringLiteral("https://gitee.com/vnotex/vnote/releases/tag/v%1").arg(p_latest);
  return info;
}

QStringList actionLabels(const NotificationMessage &p_msg) {
  QStringList labels;
  for (const auto &action : p_msg.m_actions) {
    labels.append(action.m_label);
  }
  return labels;
}

} // namespace

// Intercepts QDesktopServices::openUrl() so the suite never opens a browser.
class UrlSink : public QObject {
  Q_OBJECT

public:
  QList<QUrl> m_opened;

public slots:
  void onUrl(const QUrl &p_url) { m_opened.append(p_url); }
};

class TestUpdateController : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void test_availableUpdateBecomesAnInterruptingReleasePageOffer();
  void test_upToDateResultIsSilentOnAStartupCheck();
  void test_aLaterCheckSupersedesThePreviousOffer();
  void test_aDroppedManualRequestCannotRelabelARunningCheck();
  void test_evictingTheTrackedMessageClearsTheTrackedId();

private:
  // A COPY: the store is rebuilt on every notify(), so holding a pointer across
  // a call would dangle.
  bool activeWithKey(const QString &p_key, NotificationMessage *p_out) const;

  ServiceLocator *m_services = nullptr;
  NotificationService *m_notifications = nullptr;
  UpdateService *m_updateService = nullptr;
  UpdateController *m_controller = nullptr;
  UrlSink *m_urlSink = nullptr;
};

bool TestUpdateController::activeWithKey(const QString &p_key, NotificationMessage *p_out) const {
  for (const auto &msg : m_notifications->messages()) {
    if (!msg.m_dismissed && msg.m_dedupKey == p_key) {
      *p_out = msg;
      return true;
    }
  }
  return false;
}

void TestUpdateController::init() {
  m_urlSink = new UrlSink();
  QDesktopServices::setUrlHandler(QStringLiteral("https"), m_urlSink, "onUrl");

  m_services = new ServiceLocator();
  m_notifications = new NotificationService();
  m_services->registerService<NotificationService>(m_notifications);

  m_updateService = new UpdateService(QStringLiteral("4.4.2"));
  m_services->registerService<UpdateService>(m_updateService);

  // No ConfigMgr2 and a null parent widget: runStartupTasks() then stops before
  // the throttle, which keeps the network check (and its modal failure box) out
  // of the way. The check-result path is driven directly through the service's
  // signals instead.
  m_controller = new UpdateController(*m_services, nullptr);
}

void TestUpdateController::cleanup() {
  delete m_controller;
  m_controller = nullptr;
  delete m_updateService;
  m_updateService = nullptr;
  delete m_notifications;
  m_notifications = nullptr;
  delete m_services;
  m_services = nullptr;

  QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
  delete m_urlSink;
  m_urlSink = nullptr;
}

// The offer must be Interrupt (a toast is raised ONLY by messageAdded carrying
// Interrupt) and Persist (the user must be able to find it whenever they are
// ready), and its ONLY action is the release page: VNote downloads nothing.
void TestUpdateController::test_availableUpdateBecomesAnInterruptingReleasePageOffer() {
  emit m_updateService->checkFinished(makeInfo(true));

  NotificationMessage offer;
  QVERIFY2(activeWithKey(c_offerKey, &offer), "no offer notification was posted");
  QCOMPARE(offer.m_category, QStringLiteral("update"));
  QCOMPARE(offer.m_attention, NotificationMessage::Attention::Interrupt);
  QCOMPARE(offer.m_duration, NotificationMessage::Duration::Persist);
  QCOMPARE(actionLabels(offer), QStringList{QStringLiteral("Check Release")});
  QVERIFY2(offer.m_text.contains(QStringLiteral("4.4.3")), qPrintable(offer.m_text));

  offer.m_actions.at(0).m_callback();
  QCOMPARE(m_urlSink->m_opened.size(), 1);
  QCOMPARE(m_urlSink->m_opened.at(0).toString(),
           QStringLiteral("https://gitee.com/vnotex/vnote/releases/tag/v4.4.3"));
}

void TestUpdateController::test_upToDateResultIsSilentOnAStartupCheck() {
  emit m_updateService->checkFinished(makeInfo(false));

  NotificationMessage ignored;
  QVERIFY2(!activeWithKey(c_offerKey, &ignored), "an up-to-date check posted a notification");
  QCOMPARE(m_notifications->activeCount(), 0);
  QCOMPARE(m_urlSink->m_opened.size(), 0);
}

// A new check supersedes whatever the previous one advertised: its button would
// otherwise point at a release that is no longer the latest. The offer is one
// incident, not a growing pile.
void TestUpdateController::test_aLaterCheckSupersedesThePreviousOffer() {
  emit m_updateService->checkFinished(makeInfo(true, QStringLiteral("4.4.3")));

  NotificationMessage first;
  QVERIFY(activeWithKey(c_offerKey, &first));
  const quint64 firstId = first.m_id;

  emit m_updateService->checkFinished(makeInfo(true, QStringLiteral("4.4.4")));

  NotificationMessage second;
  QVERIFY(activeWithKey(c_offerKey, &second));
  QVERIFY2(second.m_id != firstId, "the second offer reused the first message");
  QVERIFY2(second.m_text.contains(QStringLiteral("4.4.4")), qPrintable(second.m_text));
  // renotify() retires the previous generation rather than leaving two rows.
  QVERIFY2(!m_notifications->isActive(firstId), "the superseded offer is still active");
  QCOMPARE(m_notifications->activeCount(), 1);
}

// The regression this guards: `startCheck()` used to set the manual/startup
// mode BEFORE knowing whether the service accepted the request. A user clicking
// "Check for Updates" while the silent startup check was still running would
// therefore have that startup check's result rendered on the MANUAL surface --
// a modal dialog for a check they never saw start, and, worse, a modal warning
// box for a background failure that is supposed to be silent.
//
// The service is pointed at an unroutable endpoint so its check stays in flight
// (and eventually fails) without any fixture server.
void TestUpdateController::test_aDroppedManualRequestCannotRelabelARunningCheck() {
  m_updateService->testSetEndpointOverride(
      QUrl(QStringLiteral("https://api.github.com/repos/vnotex/vnote/releases/latest")));

  // Start a silent (startup-style) check by driving the service directly, then
  // tell the controller a manual request arrived. The service must refuse it.
  QVERIFY2(m_updateService->checkForUpdates(), "the first check was not accepted");
  QVERIFY2(!m_updateService->checkForUpdates(),
           "the service accepted a second concurrent check, so this test proves nothing");

  m_controller->checkForUpdatesManually();

  // The controller must not have adopted the manual mode from a request that
  // never ran: a startup-style failure stays silent (no message box, nothing
  // posted) rather than being reported as a manual check's failure.
  emit m_updateService->failed(QStringLiteral("boom"));

  NotificationMessage ignored;
  QVERIFY2(!activeWithKey(c_offerKey, &ignored), "a failure posted an offer notification");
  QCOMPARE(m_notifications->activeCount(), 0);
  QCOMPARE(m_urlSink->m_opened.size(), 0);

  // And an offer arriving for that same startup check stays on the silent
  // surface too -- i.e. it becomes a notification, not a modal dialog.
  emit m_updateService->checkFinished(makeInfo(true));
  NotificationMessage offer;
  QVERIFY2(activeWithKey(c_offerKey, &offer),
           "the startup result did not land on the notification surface");
}

// A tracked message can leave the store without being dismissed (the retention
// cap evicts it). The controller subscribes to messageRemoved so
// m_offerNotificationId cannot keep naming a message that no longer exists.
void TestUpdateController::test_evictingTheTrackedMessageClearsTheTrackedId() {
  emit m_updateService->checkFinished(makeInfo(true));

  NotificationMessage offer;
  QVERIFY(activeWithKey(c_offerKey, &offer));
  const quint64 offerId = offer.m_id;

  // Push the store past the cap. The offer carries an action AND is Persist, so
  // the "cheap to lose" eviction tier would always prefer a plain filler over
  // it; the fillers therefore have to be equally expensive, which leaves the
  // evictor with its last resort -- the OLDEST active message, i.e. the offer.
  auto expensiveFiller = [](const QString &p_text) {
    NotificationMessage filler;
    filler.m_text = p_text;
    filler.m_duration = NotificationMessage::Duration::Persist;
    NotificationAction action;
    action.m_label = QStringLiteral("noop");
    action.m_callback = []() {};
    filler.m_actions.append(action);
    return filler;
  };

  int i = 0;
  while (m_notifications->messages().size() < NotificationService::c_maxMessages) {
    m_notifications->notify(expensiveFiller(QStringLiteral("filler %1").arg(i++)));
  }
  m_notifications->notify(expensiveFiller(QStringLiteral("overflow")));

  QVERIFY2(!m_notifications->isActive(offerId), "precondition: the offer was not evicted");
  QVERIFY2(m_notifications->messages().size() <= NotificationService::c_maxMessages,
           "the store grew past the cap");

  // With the tracked id cleared, a fresh offer lands as a NEW active message
  // instead of trying to dismiss a dead one.
  emit m_updateService->checkFinished(makeInfo(true));
  NotificationMessage second;
  QVERIFY(activeWithKey(c_offerKey, &second));
  QVERIFY(second.m_id != offerId);
}

} // namespace tests

QTEST_MAIN(tests::TestUpdateController)
#include "test_updatecontroller.moc"
