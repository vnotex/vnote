#include <QObject>
#include <QString>
#include <QtTest>

#include <core/servicelocator.h>
#include <core/services/syncstateclassifier.h>

namespace tests {

using vnotex::SyncState;
using vnotex::SyncStateClassifier;

// Unit test for SyncStateClassifier's pure predicate->state mapping.
// We exercise classifyFromPredicates() directly so we don't need to spin
// up vxcore, a keychain, or a notebook tree. classify() is a thin wrapper
// around this static helper.
class TestSyncClassifier : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void testClassifyAllEightStates_data();
  void testClassifyAllEightStates();
  void testIsPartial();
  void testIsActionable();
  void testTooltipForAllStatesNonEmpty();
};

void TestSyncClassifier::initTestCase() {
  // No vxcore needed: the static helper has no service dependencies.
  QCoreApplication::setOrganizationName("VNoteTest");
  QCoreApplication::setApplicationName("test_sync_classifier");
}

void TestSyncClassifier::testClassifyAllEightStates_data() {
  QTest::addColumn<bool>("syncEnabled");
  QTest::addColumn<bool>("hasPat");
  QTest::addColumn<bool>("registered");
  QTest::addColumn<QString>("backend");
  QTest::addColumn<QString>("remoteUrl");
  QTest::addColumn<int>("expected");

  // Use ints in the data row to keep QTest macros happy across compilers.
  // S0 — cleanly disabled.
  QTest::newRow("S0_clean_disabled")
      << false << false << false << QString() << QString() << static_cast<int>(SyncState::S0);

  // S1 — enabled, backend=git, URL empty.
  QTest::newRow("S1_no_url") << true << false << false << QStringLiteral("git") << QString()
                             << static_cast<int>(SyncState::S1);
  QTest::newRow("S1_no_url_with_pat") << true << true << false << QStringLiteral("git") << QString()
                                      << static_cast<int>(SyncState::S1);

  // S2 — enabled, backend=git, URL set, NO PAT.
  QTest::newRow("S2_no_pat") << true << false << false << QStringLiteral("git")
                             << QStringLiteral("https://example.com/r.git")
                             << static_cast<int>(SyncState::S2);

  // S3 — enabled, backend empty.
  QTest::newRow("S3_no_backend") << true << false << false << QString() << QString()
                                 << static_cast<int>(SyncState::S3);
  QTest::newRow("S3_no_backend_with_url_and_pat")
      << true << true << false << QString() << QStringLiteral("https://example.com/r.git")
      << static_cast<int>(SyncState::S3);

  // S4 — fully configured on disk but NOT registered.
  QTest::newRow("S4_disk_complete_runtime_absent")
      << true << true << false << QStringLiteral("git")
      << QStringLiteral("https://example.com/r.git") << static_cast<int>(SyncState::S4);

  // S5 — registered + ready.
  QTest::newRow("S5_ready") << true << true << true << QStringLiteral("git")
                            << QStringLiteral("https://example.com/r.git")
                            << static_cast<int>(SyncState::S5);

  // S6 — orphan PAT (disabled on disk, PAT present in keychain).
  QTest::newRow("S6_orphan_pat") << false << true << false << QString() << QString()
                                 << static_cast<int>(SyncState::S6);
}

void TestSyncClassifier::testClassifyAllEightStates() {
  QFETCH(bool, syncEnabled);
  QFETCH(bool, hasPat);
  QFETCH(bool, registered);
  QFETCH(QString, backend);
  QFETCH(QString, remoteUrl);
  QFETCH(int, expected);

  const SyncState actual = SyncStateClassifier::classifyFromPredicates(
      syncEnabled, hasPat, registered, backend, remoteUrl);
  QCOMPARE(static_cast<int>(actual), expected);
}

void TestSyncClassifier::testIsPartial() {
  // Construct a classifier without a ServiceLocator-backed dependency: the
  // partial/actionable helpers don't touch m_services, so a default-constructed
  // ServiceLocator is sufficient.
  vnotex::ServiceLocator locator;
  SyncStateClassifier classifier(locator);

  QVERIFY(!classifier.isPartial(SyncState::S0));
  QVERIFY(classifier.isPartial(SyncState::S1));
  QVERIFY(classifier.isPartial(SyncState::S2));
  QVERIFY(classifier.isPartial(SyncState::S3));
  QVERIFY(classifier.isPartial(SyncState::S4));
  QVERIFY(!classifier.isPartial(SyncState::S5));
  QVERIFY(!classifier.isPartial(SyncState::S6));
  QVERIFY(!classifier.isPartial(SyncState::S7));
}

void TestSyncClassifier::testIsActionable() {
  vnotex::ServiceLocator locator;
  SyncStateClassifier classifier(locator);

  QVERIFY(classifier.isActionable(SyncState::S0));
  QVERIFY(!classifier.isActionable(SyncState::S1));
  QVERIFY(!classifier.isActionable(SyncState::S2));
  QVERIFY(!classifier.isActionable(SyncState::S3));
  QVERIFY(!classifier.isActionable(SyncState::S4));
  QVERIFY(classifier.isActionable(SyncState::S5));
  QVERIFY(classifier.isActionable(SyncState::S6));
  QVERIFY(!classifier.isActionable(SyncState::S7));
}

void TestSyncClassifier::testTooltipForAllStatesNonEmpty() {
  vnotex::ServiceLocator locator;
  SyncStateClassifier classifier(locator);

  const SyncState all[] = {SyncState::S0, SyncState::S1, SyncState::S2, SyncState::S3,
                           SyncState::S4, SyncState::S5, SyncState::S6, SyncState::S7};
  for (SyncState s : all) {
    const QString tip = classifier.tooltipFor(s);
    QVERIFY2(!tip.isEmpty(),
             qPrintable(QStringLiteral("Empty tooltip for state %1").arg(static_cast<int>(s))));
  }
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncClassifier)
#include "test_sync_classifier.moc"
