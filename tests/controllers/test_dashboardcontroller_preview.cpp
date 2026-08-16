// DashboardController::previewStickerGeometry parity with setStickerGeometry.
//
// The drag path renders its ghost from the preview, then commits with the
// setter, so any divergence between the two is a "what you saw is not what you
// got" bug. Every case below asserts both halves against the same input.
//
// GUILESS: DashboardController is a plain QObject; StickerFactory is stubbed
// (stubs_dashboardcontroller.cpp) so no sticker widget is ever built, and no
// ConfigMgr2 is registered so persistence is a no-op.

#include <QSignalSpy>
#include <QtTest>

#include <controllers/dashboardcontroller.h>
#include <core/servicelocator.h>
#include <gui/services/stickerfactory.h>

using namespace vnotex;

namespace tests {

class TestDashboardControllerPreview : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void testUnknownIdIsInvalid();
  void testUnchangedGeometry();
  void testSelfExclusionOnResize();
  void testCollisionIsRejected();
  void testNegativeRowAndColAreClamped();
  void testColumnOverflowIsClamped();
  void testRowOverflowIsClamped();

  void testSpanBoundsAreClamped();

private:
  // Run preview + commit for one request and assert they agree, reporting the
  // normalized geometry the preview promised.
  void assertParity(const QString &p_id, const StickerGeometry &p_request,
                    StickerGeometry *p_normalized = nullptr);

  StickerGeometry geometryOf(const QString &p_id) const;

  ServiceLocator *m_services = nullptr;
  StickerFactory *m_factory = nullptr;
  DashboardController *m_controller = nullptr;

  // Mirror of the model, kept in sync from the controller's signals (the
  // records themselves are private).
  QHash<QString, StickerGeometry> m_geometries;
};

void TestDashboardControllerPreview::init() {
  m_services = new ServiceLocator();

  m_factory = new StickerFactory();
  // The stub never invokes a creator; only hasCreator() matters here.
  const QStringList types{QStringLiteral("greeting"), QStringLiteral("calendar"),
                          QStringLiteral("activity"), QStringLiteral("history")};
  for (const QString &type : types) {
    m_factory->registerCreator(type, nullptr);
  }
  m_services->registerService<StickerFactory>(m_factory);

  m_controller = new DashboardController(*m_services);
  connect(m_controller, &DashboardController::layoutReloaded, this,
          [this](const QVector<DashboardController::StickerRecord> &p_records, int) {
            m_geometries.clear();
            for (const auto &rec : p_records) {
              m_geometries.insert(rec.id,
                                  StickerGeometry{rec.row, rec.col, rec.rowSpan, rec.colSpan});
            }
          });
  connect(m_controller, &DashboardController::stickerMoved, this,
          [this](const DashboardController::StickerRecord &p_record) {
            m_geometries.insert(p_record.id, StickerGeometry{p_record.row, p_record.col,
                                                             p_record.rowSpan, p_record.colSpan});
          });

  // No ConfigMgr2 registered -> empty stored layout -> the built-in seed runs.
  m_controller->load();
  QCOMPARE(m_geometries.size(), 4);
}

void TestDashboardControllerPreview::cleanup() {
  delete m_controller;
  m_controller = nullptr;
  delete m_factory;
  m_factory = nullptr;
  delete m_services;
  m_services = nullptr;
  m_geometries.clear();
}

StickerGeometry TestDashboardControllerPreview::geometryOf(const QString &p_id) const {
  return m_geometries.value(p_id);
}

void TestDashboardControllerPreview::assertParity(const QString &p_id,
                                                  const StickerGeometry &p_request,
                                                  StickerGeometry *p_normalized) {
  StickerGeometry normalized;
  bool unchanged = false;
  const bool valid = m_controller->previewStickerGeometry(p_id, p_request, &normalized, &unchanged);

  const StickerGeometry before = geometryOf(p_id);
  QSignalSpy movedSpy(m_controller, &DashboardController::stickerMoved);
  m_controller->setStickerGeometry(p_id, p_request.row, p_request.col, p_request.rowSpan,
                                   p_request.colSpan);
  const StickerGeometry after = geometryOf(p_id);

  const bool shouldCommit = valid && !unchanged;
  if (shouldCommit) {
    // What the preview promised is exactly what was committed.
    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(after, normalized);
  } else {
    QCOMPARE(movedSpy.count(), 0);
    QCOMPARE(after, before);
  }
  if (p_normalized) {
    *p_normalized = normalized;
  }
}

void TestDashboardControllerPreview::testUnknownIdIsInvalid() {
  StickerGeometry normalized;
  bool unchanged = true;
  QVERIFY(!m_controller->previewStickerGeometry(QStringLiteral("nope"), StickerGeometry{0, 0, 1, 1},
                                                &normalized, &unchanged));
  QVERIFY(!unchanged);

  // The setter must be an equally silent no-op.
  QSignalSpy movedSpy(m_controller, &DashboardController::stickerMoved);
  m_controller->setStickerGeometry(QStringLiteral("nope"), 0, 0, 1, 1);
  QCOMPARE(movedSpy.count(), 0);
}

void TestDashboardControllerPreview::testUnchangedGeometry() {
  const QString id = QStringLiteral("s0");
  const StickerGeometry current = geometryOf(id);

  StickerGeometry normalized;
  bool unchanged = false;
  QVERIFY(m_controller->previewStickerGeometry(id, current, &normalized, &unchanged));
  QVERIFY(unchanged);
  QCOMPARE(normalized, current);

  assertParity(id, current);
}

void TestDashboardControllerPreview::testSelfExclusionOnResize() {
  // Shrinking a sticker overlaps only itself, which must not count as a
  // collision.
  const QString id = QStringLiteral("s3"); // history: row 2, col 4, 4x5
  const StickerGeometry current = geometryOf(id);
  StickerGeometry request = current;
  request.rowSpan = current.rowSpan - 1;

  StickerGeometry normalized;
  assertParity(id, request, &normalized);
  QCOMPARE(normalized, request);
  QCOMPARE(geometryOf(id), request);
}

void TestDashboardControllerPreview::testCollisionIsRejected() {
  // s0 (greeting, row 0 col 0) onto s1 (calendar, row 1 col 0).
  const QString id = QStringLiteral("s0");
  StickerGeometry request = geometryOf(id);
  request.row = geometryOf(QStringLiteral("s1")).row;

  StickerGeometry normalized;
  bool unchanged = false;
  QVERIFY(!m_controller->previewStickerGeometry(id, request, &normalized, &unchanged));
  QVERIFY(!unchanged);
  // Even a rejected request reports the normalized geometry it was judged on.
  QCOMPARE(normalized, request);

  assertParity(id, request);
}

void TestDashboardControllerPreview::testNegativeRowAndColAreClamped() {
  const QString id = QStringLiteral("s3");
  const StickerGeometry current = geometryOf(id);
  StickerGeometry request = current;
  request.row = -5;
  request.col = -3;

  StickerGeometry normalized;
  assertParity(id, request, &normalized);
  QCOMPARE(normalized.row, 0);
  QCOMPARE(normalized.col, 0);
  QCOMPARE(normalized.rowSpan, current.rowSpan);
  QCOMPARE(normalized.colSpan, current.colSpan);
}

void TestDashboardControllerPreview::testColumnOverflowIsClamped() {
  const QString id = QStringLiteral("s3");
  const StickerGeometry current = geometryOf(id);
  StickerGeometry request = current;
  request.col = 999;

  StickerGeometry normalized;
  assertParity(id, request, &normalized);
  // Clamped so the sticker still fits inside the 12-column board.
  QCOMPARE(normalized.col, m_controller->columns() - current.colSpan);
  QCOMPARE(normalized.colSpan, current.colSpan);
}

void TestDashboardControllerPreview::testRowOverflowIsClamped() {
  const QString id = QStringLiteral("s3");
  const StickerGeometry current = geometryOf(id);
  StickerGeometry request = current;
  request.row = DashboardController::kMaxRows + 1000;

  StickerGeometry normalized;
  assertParity(id, request, &normalized);
  QCOMPARE(normalized.row, DashboardController::kMaxRows);
  QCOMPARE(normalized.col, current.col);
  QCOMPARE(normalized.rowSpan, current.rowSpan);
  QCOMPARE(normalized.colSpan, current.colSpan);
}

void TestDashboardControllerPreview::testSpanBoundsAreClamped() {

  const QString id = QStringLiteral("s3");

  // colSpan is capped at the column count (and col then shifts to fit).
  StickerGeometry wide = geometryOf(id);
  wide.colSpan = 999;
  StickerGeometry normalized;
  bool unchanged = false;
  m_controller->previewStickerGeometry(id, wide, &normalized, &unchanged);
  QCOMPARE(normalized.colSpan, m_controller->columns());
  QCOMPARE(normalized.col, 0);
  assertParity(id, wide);

  // rowSpan is capped at kMaxRowSpan and floored at 1.
  StickerGeometry tall = geometryOf(id);
  tall.rowSpan = DashboardController::kMaxRowSpan + 100;
  m_controller->previewStickerGeometry(id, tall, &normalized, &unchanged);
  QCOMPARE(normalized.rowSpan, DashboardController::kMaxRowSpan);
  assertParity(id, tall);

  StickerGeometry zero = geometryOf(id);
  zero.rowSpan = 0;
  zero.colSpan = -4;
  m_controller->previewStickerGeometry(id, zero, &normalized, &unchanged);
  QCOMPARE(normalized.rowSpan, 1);
  QCOMPARE(normalized.colSpan, 1);
  assertParity(id, zero);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestDashboardControllerPreview)
#include "test_dashboardcontroller_preview.moc"
