// T21 (open-notebook-remote-readonly): NotebookCoreService::isNotebookReadOnly.
//
// SessionConfig does NOT mirror NotebookRecord -- per-notebook state lives in
// vxcore. T21 therefore adds a thin Qt-side query wrapper around
// vxcore_notebook_is_read_only so UI consumers (lock-icon badge in
// NotebookSelector2 / NotebookExplorer2, Sync Info disable/enable affordance)
// have one branch-free entry point.
//
// Verifies:
//   1. Fresh notebook (created RW) returns false.
//   2. Same notebook after vxcore_notebook_set_read_only(true) returns true.
//   3. Same notebook reopened via openNotebookEx(path, "{\"readOnly\":true}")
//      returns true -- confirms T20's persistence wiring AND T21's delegate
//      both surface the same answer.
//   4. Unknown notebook id returns false defensively (no crash, no warn).

#include <QtTest>

#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace tests {

class TestNotebookCoreServiceReadOnly : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testFreshNotebookIsNotReadOnly();
  void testFlippedReadOnlyReflectedByDelegate();
  void testReopenViaOpenNotebookExHonorsReadOnlyOption();
  void testUnknownNotebookIdReturnsFalseDefensively();

private:
  TempDirFixture m_tempDir;
  VxCoreContextHandle m_context = nullptr;
  NotebookCoreService *m_service = nullptr;
};

void TestNotebookCoreServiceReadOnly::initTestCase() {
  QVERIFY(m_tempDir.isValid());

  // CRITICAL: enable test mode BEFORE vxcore_context_create so vxcore points
  // at %TEMP%/vxcore_test* instead of real AppData.
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create("{}", &m_context);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_context != nullptr);

  m_service = new NotebookCoreService(m_context, this);
}

void TestNotebookCoreServiceReadOnly::cleanupTestCase() {
  if (m_service) {
    const QJsonArray nbs = m_service->listNotebooks();
    for (const auto &val : nbs) {
      const QString id = val.toObject().value(QStringLiteral("id")).toString();
      if (!id.isEmpty()) {
        m_service->closeNotebook(id);
      }
    }
  }
  delete m_service;
  m_service = nullptr;
  if (m_context) {
    vxcore_context_destroy(m_context);
    m_context = nullptr;
  }
}

void TestNotebookCoreServiceReadOnly::testFreshNotebookIsNotReadOnly() {
  const QString nbRoot = m_tempDir.filePath(QStringLiteral("nb_ro_fresh"));
  const QString nbId = m_service->createNotebook(
      nbRoot, QStringLiteral(R"({"name":"Fresh","description":"","version":"1"})"),
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Default per T3's runtime flag and T5's NotebookRecord default.
  QCOMPARE(m_service->isNotebookReadOnly(nbId), false);
}

void TestNotebookCoreServiceReadOnly::testFlippedReadOnlyReflectedByDelegate() {
  const QString nbRoot = m_tempDir.filePath(QStringLiteral("nb_ro_flip"));
  const QString nbId = m_service->createNotebook(
      nbRoot, QStringLiteral(R"({"name":"Flip","description":"","version":"1"})"),
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());
  QCOMPARE(m_service->isNotebookReadOnly(nbId), false);

  // Flip via the C API (test-only path; production callers go through
  // openNotebookEx or vxcore-internal session restore).
  const QByteArray idBytes = nbId.toUtf8();
  VxCoreError err = vxcore_notebook_set_read_only(m_context, idBytes.constData(), true);
  QCOMPARE(err, VXCORE_OK);

  QCOMPARE(m_service->isNotebookReadOnly(nbId), true);

  // Flip back -- delegate should track the change live.
  err = vxcore_notebook_set_read_only(m_context, idBytes.constData(), false);
  QCOMPARE(err, VXCORE_OK);
  QCOMPARE(m_service->isNotebookReadOnly(nbId), false);
}

void TestNotebookCoreServiceReadOnly::testReopenViaOpenNotebookExHonorsReadOnlyOption() {
  const QString nbRoot = m_tempDir.filePath(QStringLiteral("nb_ro_reopen"));
  const QString nbId = m_service->createNotebook(
      nbRoot, QStringLiteral(R"({"name":"Reopen","description":"","version":"1"})"),
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());
  QCOMPARE(m_service->isNotebookReadOnly(nbId), false);

  // Close + reopen with readOnly:true -- exercises T20's openNotebookEx wired
  // through T21's delegate. Verifies the two pieces compose correctly.
  QVERIFY(m_service->closeNotebook(nbId));
  const QString reopenedId =
      m_service->openNotebookEx(nbRoot, QStringLiteral("{\"readOnly\":true}"));
  QCOMPARE(reopenedId, nbId);

  QCOMPARE(m_service->isNotebookReadOnly(reopenedId), true);
}

void TestNotebookCoreServiceReadOnly::testUnknownNotebookIdReturnsFalseDefensively() {
  // No QSignalSpy / qWarning capture here -- the contract is "false on any
  // error, no noisy logging" so UI rendering can call this from paint paths
  // without flooding the log.
  QCOMPARE(m_service->isNotebookReadOnly(QStringLiteral("nb_does_not_exist")), false);
  QCOMPARE(m_service->isNotebookReadOnly(QString()), false);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestNotebookCoreServiceReadOnly)
#include "test_notebook_core_service_readonly.moc"
