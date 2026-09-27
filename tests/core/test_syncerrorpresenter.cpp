#include <QtTest>

#include <core/services/syncerrorpresenter.h>

using vnotex::SyncErrorPresenter;
using Context = SyncErrorPresenter::Context;

namespace tests {

class TestSyncErrorPresenter : public QObject {
  Q_OBJECT
private slots:
  void vaultErrorRequiresWriteContext();
  void vaultErrorTakesPrecedenceOverAuthentication();
  void errorNumberRequiresWholeToken();
  void detailsPreserveUnderlyingDiagnostic();
};

void TestSyncErrorPresenter::vaultErrorRequiresWriteContext() {
  const auto raw = QStringLiteral("Win32 error code 8: vault full");
  const auto write = SyncErrorPresenter::present(Context::CredentialWrite, VXCORE_ERR_UNKNOWN, raw);
  const auto read = SyncErrorPresenter::present(Context::CredentialRead, VXCORE_ERR_UNKNOWN, raw);
  const auto generic =
      SyncErrorPresenter::present(Context::EnableSync, VXCORE_ERR_UNKNOWN, QString());
  QVERIFY(write.primary != generic.primary);
  QCOMPARE(read.primary, generic.primary);
}

void TestSyncErrorPresenter::vaultErrorTakesPrecedenceOverAuthentication() {
  const auto raw = QStringLiteral("Win32 error code: 8");
  const auto vault = SyncErrorPresenter::present(Context::CredentialWrite, VXCORE_ERR_UNKNOWN, raw);
  const auto both =
      SyncErrorPresenter::present(Context::CredentialWrite, VXCORE_ERR_SYNC_AUTH_FAILED, raw);
  const auto auth =
      SyncErrorPresenter::present(Context::TriggerSync, VXCORE_ERR_SYNC_AUTH_FAILED, raw);
  QCOMPARE(both.primary, vault.primary);
  QVERIFY(auth.primary != vault.primary);
}

void TestSyncErrorPresenter::errorNumberRequiresWholeToken() {
  const auto eighty = SyncErrorPresenter::present(Context::CredentialWrite, VXCORE_ERR_UNKNOWN,
                                                  QStringLiteral("Win32 error code 80"));
  const auto generic =
      SyncErrorPresenter::present(Context::CredentialWrite, VXCORE_ERR_UNKNOWN, QString());
  QCOMPARE(eighty.primary, generic.primary);
}

void TestSyncErrorPresenter::detailsPreserveUnderlyingDiagnostic() {
  const auto raw = QStringLiteral("Win32 error code 8: storage unavailable");
  const auto mapped =
      SyncErrorPresenter::present(Context::CredentialWrite, VXCORE_ERR_UNKNOWN, raw);
  QCOMPARE(mapped.details, raw);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestSyncErrorPresenter)
#include "test_syncerrorpresenter.moc"
