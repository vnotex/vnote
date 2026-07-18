// GUILESS test for ActivityStatsService (read-only accessor over activity.db).
// Uses a real vxcore context in test mode, writes activity via the vxcore
// activity C API, then reads it back through the service.

#include <QtTest>

#include <QDate>

#include <core/services/activitystatsservice.h>

#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestActivityStatsService : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void test_emptyRangeIsZero();
  void test_focusTimeAggregates();
  void test_dailySeriesPopulated();
  void test_hotFilesEmptyForFutureRange();
  void test_nullContextReturnsEmpty();
  void test_invalidDatesReturnEmpty();

private:
  VxCoreContextHandle m_context = nullptr;
};

void TestActivityStatsService::initTestCase() {
  vxcore_set_test_mode(1);
  const VxCoreError err = vxcore_context_create(nullptr, &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);
}

void TestActivityStatsService::cleanupTestCase() {
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestActivityStatsService::test_emptyRangeIsZero() {
  ActivityStatsService service(m_context);
  // A far-future day has no recorded activity.
  const QDate future = QDate::currentDate().addYears(5);
  const ActivityStatsService::ActivityRange range = service.getRange(future, future);
  QCOMPARE(range.from, future);
  QCOMPARE(range.to, future);
  QCOMPARE(range.activeMs, qint64(0));
  QCOMPARE(range.notesCreated, 0);
}

void TestActivityStatsService::test_focusTimeAggregates() {
  QCOMPARE(vxcore_activity_add_focus_time(m_context, 120000), VXCORE_OK); // 2 min
  QCOMPARE(vxcore_activity_flush(m_context), VXCORE_OK);

  ActivityStatsService service(m_context);
  const QDate today = QDate::currentDate();
  const ActivityStatsService::ActivityRange range = service.getRange(today, today);
  QVERIFY(range.activeMs >= 120000);
}

void TestActivityStatsService::test_dailySeriesPopulated() {
  ActivityStatsService service(m_context);
  const QDate today = QDate::currentDate();
  // A 30-day trailing range yields a per-day series for days that have data.
  // (activity.db is shared across the test suite, so only assert today is
  // present and carries at least the focus time recorded above.)
  const ActivityStatsService::ActivityRange range = service.getRange(today.addDays(-29), today);
  QVERIFY(!range.daily.isEmpty());
  bool foundToday = false;
  for (const auto &day : range.daily) {
    QVERIFY(day.date >= today.addDays(-29));
    QVERIFY(day.date <= today);
    if (day.date == today) {
      foundToday = true;
      QVERIFY(day.activeMs >= 120000);
    }
  }
  QVERIFY(foundToday);
}

void TestActivityStatsService::test_hotFilesEmptyForFutureRange() {
  ActivityStatsService service(m_context);
  // A far-future range has no per-file activity regardless of shared state.
  const QDate future = QDate::currentDate().addYears(5);
  const QVector<ActivityStatsService::HotFile> files = service.getHotFiles(future, future, 8);
  QVERIFY(files.isEmpty());
}

void TestActivityStatsService::test_nullContextReturnsEmpty() {
  ActivityStatsService service(nullptr);
  const QDate today = QDate::currentDate();
  QCOMPARE(service.getRange(today, today).activeMs, qint64(0));
  QVERIFY(service.getHotFiles(today, today, 8).isEmpty());
}

void TestActivityStatsService::test_invalidDatesReturnEmpty() {
  ActivityStatsService service(m_context);
  QCOMPARE(service.getRange(QDate(), QDate()).daily.size(), 0);
  QVERIFY(service.getHotFiles(QDate(), QDate(), 8).isEmpty());
  // Non-positive limit yields no rows.
  QVERIFY(service.getHotFiles(QDate::currentDate(), QDate::currentDate(), 0).isEmpty());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestActivityStatsService)
#include "test_activitystatsservice.moc"
