// ManageNotebooksController::closeNotebook — hook-cancellation reason routing.
//
// Covers the in-band channel introduced when the QMessageBox was removed from
// SyncService's NotebookBeforeClose handler (core_services is Qt-Widgets-free):
//
//   handler -> HookContext::setMetadata("syncCancelReason")
//           -> HookManager::doAction(hook, event, &ctx)
//           -> NotebookCoreService::closeNotebook(id, &errorMessage)
//           -> ManageNotebooksController::closeNotebook -> result.errorMessage
//           -> ManageNotebooksDialog2 information banner
//
// Before the change, doAction discarded the HookContext, so the reason was
// structurally unreachable and the dialog always showed a hardcoded (and for
// this case, wrong) "unsaved changes" string.

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

#include <controllers/managenotebookscontroller.h>
#include <core/hookcontext.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/servicelocator.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestManageNotebooksControllerCloseReason : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void init();
  void cleanup();

  void hookReasonSurfacesAsErrorMessage();
  void cancelWithoutReasonFallsBackToGenericMessage();
  void successfulCloseLeavesErrorMessageEmpty();

private:
  QString createNotebook(const QString &p_name);

  VxCoreContextHandle m_ctx = nullptr;
  QTemporaryDir *m_tmpDir = nullptr;
  ServiceLocator *m_services = nullptr;
  HookManager *m_hookMgr = nullptr;
  NotebookCoreService *m_notebookSvc = nullptr;
};

void TestManageNotebooksControllerCloseReason::initTestCase() { vxcore_set_test_mode(1); }

void TestManageNotebooksControllerCloseReason::init() {
  QCOMPARE(vxcore_context_create("{}", &m_ctx), VXCORE_OK);
  m_tmpDir = new QTemporaryDir();
  QVERIFY(m_tmpDir->isValid());

  m_services = new ServiceLocator();
  m_hookMgr = new HookManager();
  m_services->registerService<HookManager>(m_hookMgr);
  m_notebookSvc = new NotebookCoreService(m_ctx);
  // NotebookCoreService fires (and now reads back) NotebookBeforeClose only
  // when it has a HookManager.
  m_notebookSvc->setHookManager(m_hookMgr);
  m_services->registerService<NotebookCoreService>(m_notebookSvc);
}

void TestManageNotebooksControllerCloseReason::cleanup() {
  delete m_notebookSvc;
  m_notebookSvc = nullptr;
  delete m_hookMgr;
  m_hookMgr = nullptr;
  delete m_services;
  m_services = nullptr;
  delete m_tmpDir;
  m_tmpDir = nullptr;
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

QString TestManageNotebooksControllerCloseReason::createNotebook(const QString &p_name) {
  const QString rootFolder = QDir(m_tmpDir->path()).filePath(p_name);
  if (!QDir().mkpath(rootFolder)) {
    return QString();
  }

  const QString configJson =
      QStringLiteral("{\"name\":\"%1\",\"description\":\"\",\"version\":\"1\","
                     "\"imageFolder\":\"_v_images\",\"attachmentFolder\":\"_v_attachments\","
                     "\"recycleBinFolder\":\"_v_recycle_bin\",\"createdUtc\":1700000000000}")
          .arg(p_name);
  return m_notebookSvc->createNotebook(rootFolder, configJson, NotebookType::Bundled);
}

void TestManageNotebooksControllerCloseReason::hookReasonSurfacesAsErrorMessage() {
  const QString nbId = createNotebook(QStringLiteral("nbReason"));
  QVERIFY(!nbId.isEmpty());

  const QString reason = QStringLiteral("Sync is in progress for this notebook.");
  m_hookMgr->addAction<NotebookCloseEvent>(
      HookNames::NotebookBeforeClose,
      [&reason](HookContext &p_ctx, const NotebookCloseEvent &) {
        p_ctx.cancel();
        p_ctx.setMetadata(QStringLiteral("syncCancelReason"), reason);
      },
      /*priority=*/10);

  ManageNotebooksController controller(*m_services);
  const NotebookOperationResult result = controller.closeNotebook(nbId);

  QVERIFY2(!result.success, "close must fail when the before-close hook cancels");
  QCOMPARE(result.errorMessage, reason);
  QVERIFY2(!result.errorMessage.contains(QStringLiteral("unsaved changes"), Qt::CaseInsensitive),
           "the generic fallback must not be used when the hook supplied a reason");
}

void TestManageNotebooksControllerCloseReason::cancelWithoutReasonFallsBackToGenericMessage() {
  const QString nbId = createNotebook(QStringLiteral("nbNoReason"));
  QVERIFY(!nbId.isEmpty());

  // Cancel WITHOUT stashing a reason: the controller must keep its own generic
  // message rather than surfacing an empty banner.
  m_hookMgr->addAction<NotebookCloseEvent>(
      HookNames::NotebookBeforeClose,
      [](HookContext &p_ctx, const NotebookCloseEvent &) { p_ctx.cancel(); },
      /*priority=*/10);

  ManageNotebooksController controller(*m_services);
  const NotebookOperationResult result = controller.closeNotebook(nbId);

  QVERIFY(!result.success);
  QVERIFY(!result.errorMessage.isEmpty());
  QVERIFY2(result.errorMessage.contains(QStringLiteral("unsaved changes"), Qt::CaseInsensitive),
           qPrintable(QStringLiteral("expected the generic fallback, got: %1")
                          .arg(result.errorMessage)));
}

void TestManageNotebooksControllerCloseReason::successfulCloseLeavesErrorMessageEmpty() {
  const QString nbId = createNotebook(QStringLiteral("nbOk"));
  QVERIFY(!nbId.isEmpty());

  ManageNotebooksController controller(*m_services);
  const NotebookOperationResult result = controller.closeNotebook(nbId);

  QVERIFY2(result.success, "close must succeed with no cancelling handler registered");
  QVERIFY(result.errorMessage.isEmpty());
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestManageNotebooksControllerCloseReason)
#include "test_managenotebookscontroller_closereason.moc"
