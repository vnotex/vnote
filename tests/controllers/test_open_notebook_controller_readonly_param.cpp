// Tests for OpenNotebookController::openNotebook readOnly param (T23).
//
// T23 extends OpenNotebookInput with `bool readOnly = false` and routes it
// through NotebookCoreService::openNotebookEx (T20) so the local-folder
// open path can produce a read-only notebook on demand. The local-folder
// picker in OpenNotebookDialog2 always passes readOnly=false (per plan
// guardrail "local-folder picker stays RW-only"), but the field exists so
// the clone-and-open flow (T22) can re-use openNotebook(...) for its
// post-rename re-open step AND so future session-restore paths that need
// to honor a persisted RO flag can set it.
//
// Coverage:
//   1. readOnly=false (default) -> notebook opens RW; vxcore confirms
//      is_read_only==false. Verifies T22's controller refactor did not
//      regress the back-compat path.
//   2. readOnly=true             -> notebook opens RO; vxcore confirms
//      is_read_only==true. Verifies the JSON options string is forwarded
//      through openNotebookEx correctly.

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QtTest>

#include <controllers/opennotebookcontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <temp_dir_fixture.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestOpenNotebookControllerReadOnlyParam : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanupTestCase();

  void testOpenWithReadOnlyFalseProducesRWNotebook();
  void testOpenWithReadOnlyTrueProducesROnNotebook();

private:
  VxCoreContextHandle m_ctx = nullptr;
  ServiceLocator m_services;

  // Create a bundled notebook on disk, then close it so it can be re-opened
  // via the controller. Returns the notebook root dir on success.
  QString createBundledNotebookOnDisk(TempDirFixture &p_temp, const QString &p_leafName,
                                      const QString &p_notebookName);
};

void TestOpenNotebookControllerReadOnlyParam::initTestCase() {
  // CRITICAL: enable test mode BEFORE vxcore_context_create.
  vxcore_set_test_mode(1);

  VxCoreError err = vxcore_context_create("{}", &m_ctx);
  QCOMPARE(err, VXCORE_OK);
  QVERIFY(m_ctx != nullptr);

  auto *nbSvc = new NotebookCoreService(m_ctx, this);
  m_services.registerService<NotebookCoreService>(nbSvc);
}

void TestOpenNotebookControllerReadOnlyParam::cleanupTestCase() {
  auto *nbSvc = m_services.get<NotebookCoreService>();
  if (nbSvc) {
    const QJsonArray nbs = nbSvc->listNotebooks();
    for (const auto &val : nbs) {
      const QString id = val.toObject().value(QStringLiteral("id")).toString();
      if (!id.isEmpty()) {
        nbSvc->closeNotebook(id);
      }
    }
  }
  if (m_ctx) {
    vxcore_context_destroy(m_ctx);
    m_ctx = nullptr;
  }
}

QString TestOpenNotebookControllerReadOnlyParam::createBundledNotebookOnDisk(
    TempDirFixture &p_temp, const QString &p_leafName, const QString &p_notebookName) {
  auto *nbSvc = m_services.get<NotebookCoreService>();
  const QString root = p_temp.filePath(p_leafName);
  // createNotebook needs a non-existing or empty dir for bundled type; rely
  // on TempDirFixture giving us a fresh path each call.
  const QString cfg =
      QStringLiteral(R"({"name":"%1","description":"T23 test","version":"1"})").arg(p_notebookName);
  const QString nbId = nbSvc->createNotebook(root, cfg, NotebookType::Bundled);
  if (nbId.isEmpty()) {
    return QString();
  }
  // Close so the controller can re-open via openNotebookEx.
  nbSvc->closeNotebook(nbId);
  return root;
}

void TestOpenNotebookControllerReadOnlyParam::testOpenWithReadOnlyFalseProducesRWNotebook() {
  TempDirFixture temp;
  QVERIFY(temp.isValid());

  const QString root =
      createBundledNotebookOnDisk(temp, QStringLiteral("nb_rw"), QStringLiteral("RW Notebook"));
  QVERIFY2(!root.isEmpty(), "failed to seed bundled notebook for RW open path");

  OpenNotebookController controller(m_services);

  OpenNotebookInput input;
  input.rootFolderPath = root;
  // Leave readOnly default-initialised -- this is the back-compat path
  // every existing call site relies on (per T22 commit message).

  OpenNotebookResult result = controller.openNotebook(input);
  QVERIFY2(
      result.success,
      qPrintable(QStringLiteral("RW open should succeed; error: %1").arg(result.errorMessage)));
  QVERIFY(!result.notebookId.isEmpty());

  // Authoritative check via vxcore: notebook is NOT read-only.
  bool readOnly = true;
  QCOMPARE(vxcore_notebook_is_read_only(m_ctx, result.notebookId.toUtf8().constData(), &readOnly),
           VXCORE_OK);
  QVERIFY2(!readOnly, "readOnly=false (default) input must produce an RW notebook in vxcore");
}

void TestOpenNotebookControllerReadOnlyParam::testOpenWithReadOnlyTrueProducesROnNotebook() {
  TempDirFixture temp;
  QVERIFY(temp.isValid());

  const QString root =
      createBundledNotebookOnDisk(temp, QStringLiteral("nb_ro"), QStringLiteral("RO Notebook"));
  QVERIFY2(!root.isEmpty(), "failed to seed bundled notebook for RO open path");

  OpenNotebookController controller(m_services);

  OpenNotebookInput input;
  input.rootFolderPath = root;
  input.readOnly = true; // T23: explicit opt-in to read-only.

  OpenNotebookResult result = controller.openNotebook(input);
  QVERIFY2(result.success,
           qPrintable(
               QStringLiteral("RO open should still succeed; error: %1").arg(result.errorMessage)));
  QVERIFY(!result.notebookId.isEmpty());

  bool readOnly = false;
  QCOMPARE(vxcore_notebook_is_read_only(m_ctx, result.notebookId.toUtf8().constData(), &readOnly),
           VXCORE_OK);
  QVERIFY2(readOnly, "readOnly=true input must produce a read-only notebook in vxcore");
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestOpenNotebookControllerReadOnlyParam)
#include "test_open_notebook_controller_readonly_param.moc"
