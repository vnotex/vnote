#include <QRegularExpression>
#include <QtTest>

#include <vxcore/vxcore_types.h>

#include <core/services/syncerrorpresenter.h>

namespace tests {

class TestSyncErrorPresenter : public QObject {
  Q_OBJECT

private slots:
  void testWin32ErrorEight_CredentialWrite_GivesVaultSaturationMessage();
  void testWin32ErrorEight_CredentialWrite_PreservesRawAsDetails();
  void testWin32ErrorEight_PatternVariants_data();
  void testWin32ErrorEight_PatternVariants();
  void testWin32ErrorEight_OutsideCredentialWriteContext_DoesNotGiveWindowsGuidance();
  void testAccessDenied_CredentialWrite_GivesPermissionGuidance();
  void testNotFound_CredentialRead_GivesReEnterTokenGuidance();
  void testKeychainUnavailable_GivesBuildConfigGuidance();
  void testAuthFailed_GivesAuthGuidance();
  void testNetworkError_GivesNetworkGuidance();
  void testUnknownError_FallsBackToGenericWithRawDetails();
  void testEmptyRawMessage_StillProducesUsefulPrimary();
};

// Test 1: Win32 error 8 in CredentialWrite context → vault saturation message
void TestSyncErrorPresenter::testWin32ErrorEight_CredentialWrite_GivesVaultSaturationMessage() {
  QString rawMsg = QStringLiteral("Win32 error code 8: something failed");
  auto result =
      vnotex::SyncErrorPresenter::present(vnotex::SyncErrorPresenter::Context::CredentialWrite,
                                          VXCORE_OK, // Code doesn't matter when raw pattern matches
                                          rawMsg);

  QVERIFY(result.primary.contains(QStringLiteral("Credential Manager"), Qt::CaseInsensitive));
  QVERIFY(result.primary.contains(QStringLiteral("cmdkey"), Qt::CaseInsensitive));
}

// Test 2: Win32 error 8 preserves raw message as details
void TestSyncErrorPresenter::testWin32ErrorEight_CredentialWrite_PreservesRawAsDetails() {
  QString rawMsg = QStringLiteral("Win32 error code 8: vault full");
  auto result = vnotex::SyncErrorPresenter::present(
      vnotex::SyncErrorPresenter::Context::CredentialWrite, VXCORE_OK, rawMsg);

  QCOMPARE(result.details, rawMsg);
}

// Test 3: Data-driven test for Win32 error 8 pattern variants
void TestSyncErrorPresenter::testWin32ErrorEight_PatternVariants_data() {
  QTest::addColumn<QString>("rawMessage");

  QTest::newRow("basic") << QStringLiteral("Win32 error code 8");
  QTest::newRow("with_colon") << QStringLiteral("Win32 error code: 8");
  QTest::newRow("lowercase") << QStringLiteral("win32 error code 8");
  QTest::newRow("alt_format") << QStringLiteral("Win32 error 8");
}

void TestSyncErrorPresenter::testWin32ErrorEight_PatternVariants() {
  QFETCH(QString, rawMessage);

  auto result = vnotex::SyncErrorPresenter::present(
      vnotex::SyncErrorPresenter::Context::CredentialWrite, VXCORE_OK, rawMessage);

  // All variants should produce vault saturation guidance
  QVERIFY(result.primary.contains(QStringLiteral("Credential Manager"), Qt::CaseInsensitive));
  QVERIFY(result.primary.contains(QStringLiteral("cmdkey"), Qt::CaseInsensitive));
}

// Test 4: Win32 error 8 OUTSIDE CredentialWrite context → no Windows guidance
void TestSyncErrorPresenter::
    testWin32ErrorEight_OutsideCredentialWriteContext_DoesNotGiveWindowsGuidance() {
  QString rawMsg = QStringLiteral("Win32 error code 8: vault full");

  auto result = vnotex::SyncErrorPresenter::present(vnotex::SyncErrorPresenter::Context::EnableSync,
                                                    VXCORE_OK, rawMsg);

  // Should NOT contain cmdkey guidance (context-gated)
  QVERIFY(!result.primary.contains(QStringLiteral("cmdkey"), Qt::CaseInsensitive));
}

// Test 5: "access denied" in CredentialWrite context → permission guidance
void TestSyncErrorPresenter::testAccessDenied_CredentialWrite_GivesPermissionGuidance() {
  QString rawMsg = QStringLiteral("access denied");

  auto result = vnotex::SyncErrorPresenter::present(
      vnotex::SyncErrorPresenter::Context::CredentialWrite, VXCORE_OK, rawMsg);

  // Should give permission guidance
  QVERIFY(result.primary.contains(QStringLiteral("permission"), Qt::CaseInsensitive) ||
          result.primary.contains(QStringLiteral("access"), Qt::CaseInsensitive));
}

// Test 6: "not found" in CredentialRead context → re-enter token guidance
void TestSyncErrorPresenter::testNotFound_CredentialRead_GivesReEnterTokenGuidance() {
  QString rawMsg = QStringLiteral("credential not found");

  auto result = vnotex::SyncErrorPresenter::present(
      vnotex::SyncErrorPresenter::Context::CredentialRead, VXCORE_OK, rawMsg);

  // Should guide to re-enter token
  QVERIFY(result.primary.contains(QStringLiteral("token"), Qt::CaseInsensitive) ||
          result.primary.contains(QStringLiteral("credential"), Qt::CaseInsensitive) ||
          result.primary.contains(QStringLiteral("enter"), Qt::CaseInsensitive));
}

// Test 7: "secure-keychain-unavailable" → build-without-keychain message
void TestSyncErrorPresenter::testKeychainUnavailable_GivesBuildConfigGuidance() {
  QString rawMsg = QStringLiteral("secure-keychain-unavailable");

  auto result = vnotex::SyncErrorPresenter::present(
      vnotex::SyncErrorPresenter::Context::CredentialWrite, VXCORE_OK, rawMsg);

  // Should mention keychain or build configuration
  QVERIFY(result.primary.contains(QStringLiteral("keychain"), Qt::CaseInsensitive) ||
          result.primary.contains(QStringLiteral("build"), Qt::CaseInsensitive));
}

// Test 8: VXCORE_ERR_SYNC_AUTH_FAILED → auth failure guidance
void TestSyncErrorPresenter::testAuthFailed_GivesAuthGuidance() {
  auto result = vnotex::SyncErrorPresenter::present(
      vnotex::SyncErrorPresenter::Context::TriggerSync, VXCORE_ERR_SYNC_AUTH_FAILED,
      QStringLiteral("authentication failed"));

  // Should mention authentication or credentials
  QVERIFY(result.primary.contains(QStringLiteral("auth"), Qt::CaseInsensitive) ||
          result.primary.contains(QStringLiteral("credential"), Qt::CaseInsensitive) ||
          result.primary.contains(QStringLiteral("token"), Qt::CaseInsensitive));
}

// Test 9: VXCORE_ERR_SYNC_NETWORK → network failure guidance
void TestSyncErrorPresenter::testNetworkError_GivesNetworkGuidance() {
  auto result =
      vnotex::SyncErrorPresenter::present(vnotex::SyncErrorPresenter::Context::TriggerSync,
                                          VXCORE_ERR_SYNC_NETWORK, QStringLiteral("network error"));

  // Should mention network
  QVERIFY(result.primary.contains(QStringLiteral("network"), Qt::CaseInsensitive) ||
          result.primary.contains(QStringLiteral("connection"), Qt::CaseInsensitive) ||
          result.primary.contains(QStringLiteral("internet"), Qt::CaseInsensitive));
}

// Test 10: Unknown error code → fallback to generic message with raw details
void TestSyncErrorPresenter::testUnknownError_FallsBackToGenericWithRawDetails() {
  QString rawMsg = QStringLiteral("something unexpected happened");

  auto result = vnotex::SyncErrorPresenter::present(
      vnotex::SyncErrorPresenter::Context::TriggerSync, VXCORE_ERR_UNKNOWN, rawMsg);

  // Should be generic but informative
  QVERIFY(!result.primary.isEmpty());
  QCOMPARE(result.details, rawMsg);
}

// Test 11: Empty raw message → still produces useful primary
void TestSyncErrorPresenter::testEmptyRawMessage_StillProducesUsefulPrimary() {
  auto result = vnotex::SyncErrorPresenter::present(vnotex::SyncErrorPresenter::Context::EnableSync,
                                                    VXCORE_ERR_SYNC_AUTH_FAILED, QString());

  // Primary should still be useful
  QVERIFY(!result.primary.isEmpty());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncErrorPresenter)
#include "test_syncerrorpresenter.moc"
