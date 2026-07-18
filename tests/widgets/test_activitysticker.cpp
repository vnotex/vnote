// GUI test for ActivitySticker (dashboard activity visualization). NOT GUILESS:
// constructs real QWidgets (labels, custom chart), so it needs QApplication.
// The event loop is never spun while visible.

#include <QtTest>

#include <QDate>
#include <QJsonObject>
#include <QShowEvent>
#include <QToolButton>
#include <QVariantMap>

#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/activitystatsservice.h>
#include <core/services/hookmanager.h>
#include <gui/services/stickerfactory.h>
#include <widgets/dashboard/activitysticker.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestActivitySticker : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void test_factoryRegistersActivity();
  void test_firstShowStaysToday();
  void test_calendarHookSwitchesToDate();
  void test_invalidPayloadPreservesMode();
  void test_clearFilterReturnsToToday();
  void test_destructorUnregistersCallback();

private:
  void sendShow(ActivitySticker *p_sticker) const;

  VxCoreContextHandle m_context = nullptr;
  HookManager *m_hookManager = nullptr;
  ActivityStatsService *m_statsService = nullptr;
  ServiceLocator *m_services = nullptr;
};

void TestActivitySticker::initTestCase() {
  vxcore_set_test_mode(1);

  const VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_hookManager = new HookManager();
  m_statsService = new ActivityStatsService(m_context);

  m_services = new ServiceLocator();
  m_services->registerService<HookManager>(m_hookManager);
  m_services->registerService<ActivityStatsService>(m_statsService);
}

void TestActivitySticker::cleanupTestCase() {
  delete m_services;
  m_services = nullptr;
  delete m_statsService;
  m_statsService = nullptr;
  delete m_hookManager;
  m_hookManager = nullptr;

  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestActivitySticker::sendShow(ActivitySticker *p_sticker) const {
  QShowEvent event;
  QApplication::sendEvent(p_sticker, &event);
}

void TestActivitySticker::test_factoryRegistersActivity() {
  StickerFactory factory;
  factory.registerBuiltInCreators();
  QVERIFY(factory.registeredTypes().contains(QStringLiteral("activity")));

  Sticker *sticker = factory.create(QStringLiteral("activity"), *m_services, QJsonObject());
  QVERIFY(sticker != nullptr);
  QCOMPARE(sticker->typeId(), QStringLiteral("activity"));
  QCOMPARE(sticker->titleText(), QStringLiteral("Activity"));
  delete sticker;
}

void TestActivitySticker::test_firstShowStaysToday() {
  ActivitySticker sticker(*m_services);

  // Filter bar hidden in today mode.
  auto *filterBar = sticker.findChild<QWidget *>(QStringLiteral("activityFilterBar"));
  QVERIFY(filterBar != nullptr);
  QVERIFY(filterBar->isHidden());

  sendShow(&sticker);
  // Still today mode after show: filter bar hidden.
  QVERIFY(filterBar->isHidden());
  sticker.hide();
}

void TestActivitySticker::test_calendarHookSwitchesToDate() {
  ActivitySticker sticker(*m_services);
  sendShow(&sticker);
  auto *filterBar = sticker.findChild<QWidget *>(QStringLiteral("activityFilterBar"));
  QVERIFY(filterBar->isHidden());

  QVariantMap args;
  args[QStringLiteral("date")] =
      QDate::currentDate().addDays(-1).toString(QStringLiteral("yyyy-MM-dd"));
  m_hookManager->doAction(HookNames::DashboardCalendarDateChanged, args);
  QVERIFY(!filterBar->isHidden());
  sticker.hide();
}

void TestActivitySticker::test_invalidPayloadPreservesMode() {
  ActivitySticker sticker(*m_services);
  sendShow(&sticker);
  auto *filterBar = sticker.findChild<QWidget *>(QStringLiteral("activityFilterBar"));
  QVERIFY(filterBar->isHidden());

  m_hookManager->doAction(HookNames::DashboardCalendarDateChanged, QVariantMap());
  QVERIFY(filterBar->isHidden());

  QVariantMap bad;
  bad[QStringLiteral("date")] = QStringLiteral("not-a-date");
  m_hookManager->doAction(HookNames::DashboardCalendarDateChanged, bad);
  QVERIFY(filterBar->isHidden());
  sticker.hide();
}

void TestActivitySticker::test_clearFilterReturnsToToday() {
  ActivitySticker sticker(*m_services);
  sendShow(&sticker);
  auto *filterBar = sticker.findChild<QWidget *>(QStringLiteral("activityFilterBar"));

  QVariantMap args;
  args[QStringLiteral("date")] =
      QDate::currentDate().addDays(-2).toString(QStringLiteral("yyyy-MM-dd"));
  m_hookManager->doAction(HookNames::DashboardCalendarDateChanged, args);
  QVERIFY(!filterBar->isHidden());

  auto *clearButton = sticker.findChild<QToolButton *>(QStringLiteral("activityClearFilter"));
  QVERIFY(clearButton != nullptr);
  clearButton->click();
  QVERIFY(filterBar->isHidden());
  sticker.hide();
}

void TestActivitySticker::test_destructorUnregistersCallback() {
  const int before = m_hookManager->actionCount(HookNames::DashboardCalendarDateChanged);

  auto *sticker = new ActivitySticker(*m_services);
  QCOMPARE(m_hookManager->actionCount(HookNames::DashboardCalendarDateChanged), before + 1);

  delete sticker;
  QCOMPARE(m_hookManager->actionCount(HookNames::DashboardCalendarDateChanged), before);

  QVariantMap args;
  args[QStringLiteral("date")] = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
  m_hookManager->doAction(HookNames::DashboardCalendarDateChanged, args);
}

} // namespace tests

QTEST_MAIN(tests::TestActivitySticker)
#include "test_activitysticker.moc"
