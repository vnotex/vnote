#include <QObject>
#include <QScopeGuard>
#include <QString>
#include <QtTest>
#include <core/services/syncsettings.h>
#include <vxcore/vxcore.h>

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
  void testJianguoyunEndpointPolicy_data();
  void testJianguoyunEndpointPolicy();
  void testJianguoyunTestEndpointRequiresCoreMode();
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
  for (const auto &backend :
       {QStringLiteral("git"), QStringLiteral("webdav"), QStringLiteral("jianguoyun")}) {
    const auto remote = backend == QLatin1String("jianguoyun")
                            ? QStringLiteral("https://dav.jianguoyun.com/dav/notebook/")
                            : QStringLiteral("https://example.com/notebook/");
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

void TestSyncClassifier::testJianguoyunEndpointPolicy_data() {
  QTest::addColumn<QString>("url");
  QTest::addColumn<QString>("canonical");
  QTest::newRow("collection") << QStringLiteral(" https://DAV.JIANGUOYUN.COM:443/dav/notebook ")
                              << QStringLiteral("https://dav.jianguoyun.com/dav/notebook/");
  QTest::newRow("nested-encoded")
      << QStringLiteral("https://dav.jianguoyun.com/dav/notes/my%20book/")
      << QStringLiteral("https://dav.jianguoyun.com/dav/notes/my%20book/");
  const QStringList rejected = {QStringLiteral("http://dav.jianguoyun.com/dav/notebook/"),
                                QStringLiteral("https://example.com/dav/notebook/"),
                                QStringLiteral("https://dav.jianguoyun.com:444/dav/notebook/"),
                                QStringLiteral("https://dav.jianguoyun.com/dav/"),
                                QStringLiteral("https://dav.jianguoyun.com/dav"),
                                QStringLiteral("https://dav.jianguoyun.com/"),
                                QStringLiteral("https://dav.jianguoyun.com/notes/"),
                                QStringLiteral("https://user@dav.jianguoyun.com/dav/notebook/"),
                                QStringLiteral("https://dav.jianguoyun.com/dav/notebook/?a=1"),
                                QStringLiteral("https://dav.jianguoyun.com/dav/notebook/#fragment"),
                                QStringLiteral("https://dav.jianguoyun.com/dav/a//b/"),
                                QStringLiteral("https://dav.jianguoyun.com/dav/notebook//"),
                                QStringLiteral("https://dav.jianguoyun.com/dav/%2e%2e/book/"),
                                QStringLiteral("https://dav.jianguoyun.com/dav/a%2Fb/"),
                                QStringLiteral("https://dav.jianguoyun.com/dav/a%5Cb/"),
                                QStringLiteral("https://dav.jianguoyun.com/dav/%00/")};
  for (const auto &url : rejected)
    QTest::newRow(url.toUtf8().constData()) << url << QString();
}

void TestSyncClassifier::testJianguoyunEndpointPolicy() {
  QFETCH(QString, url);
  QFETCH(QString, canonical);
  vnotex::SyncSettings settings;
  settings.m_backend = QStringLiteral("jianguoyun");
  settings.m_remoteUrl = url;
  QCOMPARE(vnotex::canonicalSyncRemoteUrl(settings), canonical);
  QCOMPARE(vnotex::validateSyncSettings(settings, false).isEmpty(), !canonical.isEmpty());
  settings.m_backend = QStringLiteral("webdav");
  settings.m_remoteUrl = QStringLiteral("http://other.example.test/dav/");
  QCOMPARE(vnotex::canonicalSyncRemoteUrl(settings), settings.m_remoteUrl);
}

void TestSyncClassifier::testJianguoyunTestEndpointRequiresCoreMode() {
  const auto previousMode = vxcore_is_test_mode();
  const bool hadUrl = qEnvironmentVariableIsSet("VXCORE_WEBDAV_TEST_URL");
  const auto previousUrl = qgetenv("VXCORE_WEBDAV_TEST_URL");
  const auto restore = qScopeGuard([&]() {
    vxcore_set_test_mode(previousMode);
    if (hadUrl)
      qputenv("VXCORE_WEBDAV_TEST_URL", previousUrl);
    else
      qunsetenv("VXCORE_WEBDAV_TEST_URL");
  });
  vnotex::SyncSettings settings;
  settings.m_backend = QStringLiteral("jianguoyun");
  settings.m_remoteUrl = QStringLiteral("https://127.0.0.1:9443/test-notebook/");
  qputenv("VXCORE_WEBDAV_TEST_URL", settings.m_remoteUrl.toUtf8());
  vxcore_set_test_mode(0);
  QVERIFY(vnotex::canonicalSyncRemoteUrl(settings).isEmpty());
  vxcore_set_test_mode(1);
  QCOMPARE(vnotex::canonicalSyncRemoteUrl(settings), settings.m_remoteUrl);
  settings.m_remoteUrl += QStringLiteral("sibling/");
  QVERIFY(vnotex::canonicalSyncRemoteUrl(settings).isEmpty());
  for (const auto &url : {QStringLiteral("http://127.0.0.1:9443/test-notebook/"),
                          QStringLiteral("https://example.test:9443/test-notebook/")}) {
    settings.m_remoteUrl = url;
    qputenv("VXCORE_WEBDAV_TEST_URL", url.toUtf8());
    QVERIFY(vnotex::canonicalSyncRemoteUrl(settings).isEmpty());
  }
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
