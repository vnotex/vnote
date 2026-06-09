// T29 follow-up: NotebookSyncInfoDialog2 read-only banner visibility tests.
//
// Verifies that the read-only banner widget (objectName "readOnlyBannerLabel")
// added at the top of NotebookSyncInfoDialog2 in T29:
//   1. Is VISIBLE when the dialog is opened for a read-only notebook (queried
//      via NotebookCoreService::isNotebookReadOnly which itself wraps the T21
//      vxcore_notebook_is_read_only ABI).
//   2. Is HIDDEN when the dialog is opened for a writable notebook.
//   3. Is HIDDEN when the dialog is constructed in pre-create mode (no
//      notebook ID is bound; refreshReadOnlyBanner early-returns and keeps
//      the banner hidden by construction).
//
// NOT GUILESS — needs QApplication for QDialog widget construction and the
// QLabel visibility checks. Mirrors the source layout of the existing
// test_notebooksyncinfodialog2 family in tests/widgets/CMakeLists.txt
// (the dialog cpp + its base widgets are compiled inline because there is
// no separate widgets static library in this project — those .cpps live
// inside the vnote executable target).
//
// The read-only state is established the same way as T26's selector badge
// test (test_notebook_selector2_readonly_badge): create a bundled notebook
// via NotebookCoreService, then flip its read-only flag via the vxcore C
// ABI vxcore_notebook_set_read_only.

#include <QApplication>
#include <QLabel>
#include <QtTest>

#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>
#include <widgets/dialogs/notebooksyncinfodialog2.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestNotebookSyncInfoDialog2ReadOnly : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  // 1. RO notebook -> banner visible.
  void testBannerVisibleOnReadOnlyNotebook();

  // 2. Writable notebook -> banner hidden.
  void testBannerHiddenOnWritableNotebook();

  // 3. Pre-create constructor -> banner hidden (no notebook ID yet).
  void testPreCreateModeNoBanner();

private:
  VxCoreContextHandle m_ctx = nullptr;
};

void TestNotebookSyncInfoDialog2ReadOnly::initTestCase() {
  // CRITICAL: enable test mode BEFORE any vxcore_context_create. Per
  // tests/AGENTS.md, this prevents tests from corrupting real user data
  // by redirecting vxcore's AppData paths to %TEMP%/vxcore_test*.
  vxcore_set_test_mode(1);
  QCOMPARE(vxcore_context_create("{}", &m_ctx), VXCORE_OK);
  QVERIFY(m_ctx != nullptr);
}

void TestNotebookSyncInfoDialog2ReadOnly::cleanupTestCase() {
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

// =============================================================================
// Subtest 1: Read-only notebook -> banner visible.
// =============================================================================
void TestNotebookSyncInfoDialog2ReadOnly::testBannerVisibleOnReadOnlyNotebook() {
  ServiceLocator services;
  NotebookCoreService notebookService(m_ctx);
  services.registerService<NotebookCoreService>(&notebookService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  const QString nbRoot = localTemp.filePath(QStringLiteral("nb_ro_banner_visible"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"RO Banner Visible","description":"T29 follow-up"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Flip to read-only via the vxcore C ABI (mirrors T26 test pattern).
  const VxCoreError err = vxcore_notebook_set_read_only(m_ctx, nbId.toUtf8().constData(), true);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(notebookService.isNotebookReadOnly(nbId));

  NotebookSyncInfoDialog2 dialog(services, nbId);
  // The banner is shown lazily by ScrollDialog when the dialog becomes
  // visible (some QLabel visibility properties only initialize after show).
  // Use show() so the widget tree enters the visible state without blocking
  // on a modal exec().
  dialog.show();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  auto *banner = dialog.findChild<QLabel *>(QStringLiteral("readOnlyBannerLabel"));
  QVERIFY2(banner, "Read-only banner QLabel must exist with objectName 'readOnlyBannerLabel'");
  QVERIFY2(banner->isVisible(), "Banner must be visible when the notebook is read-only");
  QVERIFY2(!banner->text().isEmpty(),
           "Banner text must not be empty — should explain the close-and-reopen flow");

  dialog.hide();
  // Cleanup: flip back to writable then close so subsequent subtests start
  // from a clean vxcore state.
  vxcore_notebook_set_read_only(m_ctx, nbId.toUtf8().constData(), false);
  notebookService.closeNotebook(nbId);
}

// =============================================================================
// Subtest 2: Writable notebook -> banner hidden.
// =============================================================================
void TestNotebookSyncInfoDialog2ReadOnly::testBannerHiddenOnWritableNotebook() {
  ServiceLocator services;
  NotebookCoreService notebookService(m_ctx);
  services.registerService<NotebookCoreService>(&notebookService);

  TempDirFixture localTemp;
  QVERIFY(localTemp.isValid());

  const QString nbRoot = localTemp.filePath(QStringLiteral("nb_ro_banner_hidden"));
  QDir().mkpath(nbRoot);
  const QString nbId = notebookService.createNotebook(
      nbRoot, R"({"name":"RO Banner Hidden","description":"T29 follow-up writable"})",
      NotebookType::Bundled);
  QVERIFY(!nbId.isEmpty());

  // Sanity: a freshly-created notebook is writable by default.
  QVERIFY(!notebookService.isNotebookReadOnly(nbId));

  NotebookSyncInfoDialog2 dialog(services, nbId);
  dialog.show();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  auto *banner = dialog.findChild<QLabel *>(QStringLiteral("readOnlyBannerLabel"));
  // The banner widget MUST exist (setupUI constructs it unconditionally) but
  // MUST NOT be visible for a writable notebook.
  QVERIFY2(banner, "Read-only banner QLabel must exist even on writable notebooks");
  QVERIFY2(!banner->isVisible(), "Banner must be hidden when the notebook is writable");

  dialog.hide();
  notebookService.closeNotebook(nbId);
}

// =============================================================================
// Subtest 3: Pre-create mode -> banner hidden (no notebook ID yet).
// =============================================================================
void TestNotebookSyncInfoDialog2ReadOnly::testPreCreateModeNoBanner() {
  ServiceLocator services;
  NotebookCoreService notebookService(m_ctx);
  services.registerService<NotebookCoreService>(&notebookService);

  // Pre-create constructor: takes only (services, parent) — no notebook ID.
  // The dialog's m_preCreateMode flag is set true and refreshReadOnlyBanner
  // early-returns leaving the banner hidden by construction.
  NotebookSyncInfoDialog2 dialog(services);
  QVERIFY(dialog.isPreCreateMode());

  dialog.show();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

  auto *banner = dialog.findChild<QLabel *>(QStringLiteral("readOnlyBannerLabel"));
  // setupUI still constructs the banner in pre-create mode (it's a generic
  // widget; the visibility decision is made elsewhere). The contract is:
  // not visible because there's no notebook to query.
  QVERIFY2(banner, "Read-only banner widget must be constructed in pre-create mode");
  QVERIFY2(!banner->isVisible(), "Banner must be hidden in pre-create mode (no notebook to query)");

  dialog.hide();
}

} // namespace tests

QTEST_MAIN(tests::TestNotebookSyncInfoDialog2ReadOnly)
#include "test_notebook_sync_info_dialog2_readonly.moc"
