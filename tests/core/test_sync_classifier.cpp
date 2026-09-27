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
};

void TestSyncClassifier::initTestCase() {
  // No vxcore needed: the static helper has no service dependencies.
  QCoreApplication::setOrganizationName("VNoteTest");
  QCoreApplication::setApplicationName("test_sync_classifier");
}

void TestSyncClassifier::testClassifyAllEightStates_data() {
  QTest::addColumn<bool>("syncEnabled");
  QTest::addColumn<bool>("hasCredentials");
  QTest::addColumn<bool>("registered");
  QTest::addColumn<QString>("backend");
  QTest::addColumn<QString>("remoteUrl");
  QTest::addColumn<int>("expected");
  for (const auto &backend : {QStringLiteral("git"), QStringLiteral("webdav")}) {
    const auto remote = QStringLiteral("https://example.com/notebook/");
    const auto prefix = backend.toLatin1();
    QTest::newRow((prefix + "-S0").constData())
        << false << false << false << backend << QString() << int(SyncState::S0);
    QTest::newRow((prefix + "-S1").constData())
        << true << true << false << backend << QString() << int(SyncState::S1);
    QTest::newRow((prefix + "-S2").constData())
        << true << false << false << backend << remote << int(SyncState::S2);
    QTest::newRow((prefix + "-S4").constData())
        << true << true << false << backend << remote << int(SyncState::S4);
    QTest::newRow((prefix + "-S5").constData())
        << true << true << true << backend << remote << int(SyncState::S5);
    QTest::newRow((prefix + "-S6-stale-runtime").constData())
        << false << true << true << backend << remote << int(SyncState::S6);
    QTest::newRow((prefix + "-disabled-stale-runtime").constData())
        << false << false << true << backend << remote << int(SyncState::S0);
  }
  QTest::newRow("S3-missing-backend")
      << true << true << true << QString() << QStringLiteral("https://example.com/")
      << int(SyncState::S3);
  QTest::newRow("S3-unknown-registered-backend")
      << true << true << true << QStringLiteral("unrecognized")
      << QStringLiteral("https://example.com/") << int(SyncState::S3);
}

void TestSyncClassifier::testClassifyAllEightStates() {
  QFETCH(bool, syncEnabled);
  QFETCH(bool, hasCredentials);
  QFETCH(bool, registered);
  QFETCH(QString, backend);
  QFETCH(QString, remoteUrl);
  QFETCH(int, expected);

  const SyncState actual = SyncStateClassifier::classifyFromPredicates(
      syncEnabled, hasCredentials, registered, backend, remoteUrl);
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

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncClassifier)
#include "test_sync_classifier.moc"
