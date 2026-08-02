// NotificationPopup2 + NotificationButton2: the persistent notification centre.
//
// Focus is the retention path: a message can leave the store WITHOUT being
// dismissed (renotify() replacement, or the retention cap evicting it), and
// every renderer must drop that id. A stale row would keep an inert button on
// screen; a stale badge would misreport the active count.

#include <QtTest>

#include <QLabel>
#include <QPushButton>
#include <QFileInfo>
#include <QToolButton>

#include <core/servicelocator.h>
#include <core/services/notificationservice.h>
#include <gui/services/themeservice.h>
#include <widgets/notificationbutton2.h>
#include <widgets/notificationpopup2.h>

using namespace vnotex;

namespace tests {

class TestNotificationPopup2 : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void test_visiblePopupDropsAnEvictedRow();
  void test_visiblePopupShowsAMessageThatArrivesWhileOpen();
  void test_dismissedMessagesAreNotRendered();
  void test_detailsAreRenderedCollapsed();
  void test_badgeTracksActiveCountAcrossEviction();

private:
  // Rows are QFrames holding the message text; count the rendered texts.
  QStringList renderedTexts() const;

  static void fillTo(NotificationService &p_service, int p_target);

  // QMenu::show() does NOT emit aboutToShow (only popup()/exec() do), and
  // aboutToShow is what seeds the initial rebuild in production. popup() would
  // take a mouse+keyboard grab, so seed it explicitly instead; isVisible() is
  // then true and the signal-driven refreshes behave exactly as they do live.
  void openPopup();

  ServiceLocator *m_services = nullptr;
  NotificationService *m_notifications = nullptr;
  ThemeService *m_themeService = nullptr;
  NotificationPopup2 *m_popup = nullptr;
};

QStringList TestNotificationPopup2::renderedTexts() const {
  // rebuild() retires old rows with deleteLater(), so without draining the
  // deferred-delete queue the previous generation's labels are still children
  // and every "row disappeared" assertion would be a false negative.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  QStringList out;
  for (auto *label : m_popup->findChildren<QLabel *>()) {
    // isHidden() rather than isVisible(): the popup's ancestors are not shown in
    // a test, so isVisible() would be false for everything. isHidden() reflects
    // the widget's own state, which is what the Details disclosure toggles.
    if (!label->isHidden() && !label->text().isEmpty()) {
      out.append(label->text());
    }
  }
  return out;
}

void TestNotificationPopup2::fillTo(NotificationService &p_service, int p_target) {
  int i = 0;
  while (p_service.messages().size() < p_target) {
    NotificationMessage msg;
    msg.m_text = QStringLiteral("filler %1").arg(i++);
    p_service.notify(msg);
  }
}

void TestNotificationPopup2::openPopup() {
  m_popup->show();
  m_popup->rebuild();
}

void TestNotificationPopup2::init() {
  m_services = new ServiceLocator();
  m_notifications = new NotificationService();
  m_services->registerService<NotificationService>(m_notifications);

  // TitleBar (used by the popup header) dereferences ThemeService
  // unconditionally, and ThemeService throws EssentialFileMissing when its
  // appDataPath contains no themes/ -- so point it at the bundled themes rather
  // than an empty temp dir. Nothing here asserts on theming; this is just
  // enough for the popup to construct.
  QString pure = QFINDTESTDATA("../../src/data/extra/themes/pure");
  if (pure.isEmpty()) {
    pure = QFINDTESTDATA("src/data/extra/themes/pure");
  }
  QVERIFY2(!pure.isEmpty(), "bundled 'pure' theme not found");

  ThemeServiceConfig themeConfig;
  themeConfig.themeName = QStringLiteral("pure");
  themeConfig.locale = QStringLiteral("en_US");
  // appDataPath is the PARENT of themes/.
  themeConfig.appDataPath = QFileInfo(QFileInfo(pure).absolutePath()).absolutePath();
  m_themeService = new ThemeService(themeConfig);
  m_services->registerService<ThemeService>(m_themeService);

  m_popup = new NotificationPopup2(*m_services, nullptr);
}

void TestNotificationPopup2::cleanup() {
  delete m_popup;
  m_popup = nullptr;
  delete m_themeService;
  m_themeService = nullptr;
  delete m_notifications;
  m_notifications = nullptr;
  delete m_services;
  m_services = nullptr;
}

// The regression: eviction emits messageRemoved (NOT messageDismissed), so a
// popup that only listened to dismissal would keep rendering a row whose
// message no longer exists, with buttons that resolve to nothing.
void TestNotificationPopup2::test_visiblePopupDropsAnEvictedRow() {
  NotificationMessage victim;
  victim.m_text = QStringLiteral("evict-me");
  const quint64 victimId = m_notifications->notify(victim);

  fillTo(*m_notifications, NotificationService::c_maxMessages);

  openPopup();
  QVERIFY(m_popup->isVisible());
  QVERIFY2(renderedTexts().contains(QStringLiteral("evict-me")),
           "precondition: the victim should be rendered before eviction");

  // One more append evicts exactly one message; the victim is the oldest.
  m_notifications->notify(NotificationMessage());

  QVERIFY2(!m_notifications->isActive(victimId), "precondition: the victim was not evicted");
  QVERIFY2(!renderedTexts().contains(QStringLiteral("evict-me")),
           "the popup kept a row for a message that no longer exists");

  m_popup->hide();
}

// Nothing auto-pops any more, so without a messageAdded connection an already
// open popup would silently go stale.
void TestNotificationPopup2::test_visiblePopupShowsAMessageThatArrivesWhileOpen() {
  openPopup();
  QVERIFY(m_popup->isVisible());
  QVERIFY(!renderedTexts().contains(QStringLiteral("arrived")));

  NotificationMessage msg;
  msg.m_text = QStringLiteral("arrived");
  m_notifications->notify(msg);

  QVERIFY2(renderedTexts().contains(QStringLiteral("arrived")),
           "an open popup did not pick up a newly added message");

  m_popup->hide();
}

void TestNotificationPopup2::test_dismissedMessagesAreNotRendered() {
  NotificationMessage msg;
  msg.m_text = QStringLiteral("gone");
  const quint64 id = m_notifications->notify(msg);

  openPopup();
  QVERIFY(renderedTexts().contains(QStringLiteral("gone")));

  m_notifications->dismiss(id);
  QVERIFY(!renderedTexts().contains(QStringLiteral("gone")));

  m_popup->hide();
}

// m_details is the home for what used to be QMessageBox::setDetailedText. It
// belongs to the popup only, and starts collapsed so a long blob does not
// dominate the list.
void TestNotificationPopup2::test_detailsAreRenderedCollapsed() {
  NotificationMessage msg;
  msg.m_text = QStringLiteral("summary");
  msg.m_details = QStringLiteral("the long backend explanation");
  m_notifications->notify(msg);

  openPopup();

  QPushButton *toggle = nullptr;
  for (auto *btn : m_popup->findChildren<QPushButton *>()) {
    if (btn->text() == QObject::tr("Details")) {
      toggle = btn;
      break;
    }
  }
  QVERIFY2(toggle, "no Details disclosure was rendered for a message with details");
  QVERIFY2(!renderedTexts().contains(QStringLiteral("the long backend explanation")),
           "the details blob was expanded by default");

  toggle->click();
  QVERIFY2(renderedTexts().contains(QStringLiteral("the long backend explanation")),
           "toggling Details did not reveal the blob");

  m_popup->hide();
}

void TestNotificationPopup2::test_badgeTracksActiveCountAcrossEviction() {
  NotificationButton2 button(*m_services, QSize(16, 16));

  m_notifications->notify(NotificationMessage());
  const quint64 victimId = m_notifications->notify(NotificationMessage());
  QCOMPARE(button.testBadgeCount(), 2);

  // Dismissal is the easy case (messageDismissed has always been wired).
  m_notifications->dismiss(victimId);
  QCOMPARE(button.testBadgeCount(), 1);

  // Eviction is the regression: it emits messageRemoved, NOT messageDismissed.
  // A button wired only to dismissal would keep reporting the stale count.
  int i = 0;
  while (m_notifications->messages().size() < NotificationService::c_maxMessages) {
    NotificationMessage filler;
    filler.m_text = QStringLiteral("filler %1").arg(i++);
    m_notifications->notify(filler);
  }
  const int beforeEviction = button.testBadgeCount();
  QCOMPARE(beforeEviction, m_notifications->activeCount());

  // Past the cap: one existing message is evicted, one is appended. The
  // dismissed victim is the preferred victim, so the ACTIVE count rises by one.
  m_notifications->notify(NotificationMessage());

  QVERIFY2(m_notifications->messages().size() <= NotificationService::c_maxMessages,
           "the store grew past the cap");
  QVERIFY2(button.testBadgeCount() == m_notifications->activeCount(),
           "the badge did not follow the active count across an eviction");
  QCOMPARE(button.testBadgeCount(), beforeEviction + 1);
}

} // namespace tests

QTEST_MAIN(tests::TestNotificationPopup2)
#include "test_notificationpopup2.moc"
