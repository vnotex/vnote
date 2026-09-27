// Real registered-notebook conflict resolution through SyncService, controller and dialog.
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QWidget>
#include <QtTest>

#include <controllers/syncconflictcontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notebookiogate.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <widgets/dialogs/syncconflictdialog2.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

namespace {

// Drain pending DeferredDelete events so dialogs from a previous case don't
// linger in QApplication::topLevelWidgets().
void drainPendingEvents() {
  for (int i = 0; i < 20; ++i) {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
  }
}

SyncConflictDialog2 *findOpenDialog() {
  for (QWidget *w : QApplication::topLevelWidgets()) {
    if (auto *d = qobject_cast<SyncConflictDialog2 *>(w)) {
      if (d->isVisible()) {
        return d;
      }
    }
  }
  return nullptr;
}

void connectConflicts(SyncService &p_service, SyncConflictController &p_controller) {
  QObject::connect(&p_service, &SyncService::conflictsDetected, &p_controller,
                   [&p_controller](const QString &p_id, const QStringList &p_files) {
                     p_controller.presentConflicts(p_id, p_files, nullptr);
                   });
}

bool runGit(const QStringList &p_arguments, QByteArray *p_output = nullptr) {
  QProcess process;
  process.start(QStringLiteral("git"), p_arguments);
  if (!process.waitForFinished(15000) || process.exitStatus() != QProcess::NormalExit ||
      process.exitCode() != 0) {
    qWarning().noquote() << process.readAllStandardError();
    return false;
  }
  if (p_output)
    *p_output = process.readAllStandardOutput();
  return true;
}

bool writeNote(const QString &p_path, const QByteArray &p_bytes) {
  QFile file(p_path);
  return file.open(QIODevice::WriteOnly) && file.write(p_bytes) == p_bytes.size();
}

} // namespace

class TestSyncE2E : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();

  void cancelLeavesSyncBlocked();
  void registeredNotebookConflictResolution();
};

void TestSyncE2E::initTestCase() {
  // CRITICAL: enable test mode BEFORE any vxcore_context_create (per
  // tests/AGENTS.md). Prevents tests from corrupting real user data.
  vxcore_set_test_mode(1);
}

void TestSyncE2E::cancelLeavesSyncBlocked() {
  drainPendingEvents();

  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);

  ServiceLocator services;
  NotebookCoreService notebookService(ctx);
  services.registerService<NotebookCoreService>(&notebookService);
  SyncCredentialsStore credStore(services);
  services.registerService<SyncCredentialsStore>(&credStore);
  SyncService syncService(services);
  services.registerService<SyncService>(&syncService);

  SyncConflictController controller(services);
  connectConflicts(syncService, controller);

  QSignalSpy abandonSpy(&controller, &SyncConflictController::conflictsAbandoned);
  QSignalSpy resolveSpy(&controller, &SyncConflictController::conflictsResolved);
  QSignalSpy syncFinishedSpy(&syncService, &SyncService::syncFinished);

  const QString nbId = QStringLiteral("nb_test_e2e_cancel");
  const QStringList files{QStringLiteral("a.md")};

  emit syncService.conflictsDetected(nbId, files);

  for (int i = 0; i < 50 && findOpenDialog() == nullptr; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QTest::qWait(20);
  }
  SyncConflictDialog2 *dlg = findOpenDialog();
  QVERIFY(dlg != nullptr);

  QPushButton *cancelBtn = dlg->findChild<QPushButton *>(QStringLiteral("cancelButton"));
  QVERIFY(cancelBtn != nullptr);
  cancelBtn->click();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

  QCOMPARE(abandonSpy.count(), 1);
  QCOMPARE(abandonSpy.first().at(0).toString(), nbId);

  // Give the worker a window to surface any spurious sync activity.
  for (int i = 0; i < 10; ++i) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QTest::qWait(20);
  }
  QCOMPARE(resolveSpy.count(), 0);
  QCOMPARE(syncFinishedSpy.count(), 0);

  drainPendingEvents();
  vxcore_context_destroy(ctx);
}

void TestSyncE2E::registeredNotebookConflictResolution() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  VxCoreContextHandle raw = nullptr;
  QCOMPARE(vxcore_context_create("{}", &raw), VXCORE_OK);
  const auto release = qScopeGuard([&]() { vxcore_context_destroy(raw); });
  ServiceLocator services;
  NotebookIoGate gate;
  services.registerService<NotebookIoGate>(&gate);
  NotebookCoreService notebooks(raw);
  notebooks.setNotebookIoGate(&gate);
  services.registerService<NotebookCoreService>(&notebooks);
  SyncCredentialsStore credentials(services);
  services.registerService<SyncCredentialsStore>(&credentials);
  SyncService sync(services);
  services.registerService<SyncService>(&sync);
  SyncConflictController controller(services);
  connectConflicts(sync, controller);

  const auto remote = directory.filePath(QStringLiteral("remote.git"));
  QVERIFY(runGit({"init", "--bare", "--initial-branch=main", remote}));
  const auto root = directory.filePath(QStringLiteral("notebook"));
  const auto id = notebooks.createNotebook(root, QStringLiteral(R"({"name":"Conflict E2E"})"),
                                           NotebookType::Bundled);
  QVERIFY(!id.isEmpty());
  QVERIFY(writeNote(root + QStringLiteral("/note.md"), "baseline\n"));
  const auto url = QUrl::fromLocalFile(remote).toString();
  const QJsonObject config{{"backend", "git"}, {"remoteUrl", url}, {"autoSyncEnabled", false}};
  QCOMPARE(notebooks.enableSync(id, QString::fromUtf8(QJsonDocument(config).toJson()),
                                QStringLiteral(R"({"pat":"local-test-only"})")),
           VXCORE_OK);
  auto notebookConfig = notebooks.getNotebookConfig(id);
  notebookConfig[QStringLiteral("syncEnabled")] = true;
  notebookConfig[QStringLiteral("syncBackend")] = QStringLiteral("git");
  notebookConfig[QStringLiteral("syncRemoteUrl")] = url;
  QVERIFY(notebooks.updateNotebookConfig(
      id, QString::fromUtf8(QJsonDocument(notebookConfig).toJson())));

  const auto peer = directory.filePath(QStringLiteral("peer"));
  QVERIFY(runGit({"clone", remote, peer}));
  QVERIFY(runGit({"-C", peer, "config", "user.name", "Conflict peer"}));
  QVERIFY(runGit({"-C", peer, "config", "user.email", "peer@example.invalid"}));
  QVERIFY(writeNote(peer + QStringLiteral("/note.md"), "remote winner\n"));
  QVERIFY(runGit({"-C", peer, "add", "note.md"}));
  QVERIFY(runGit({"-C", peer, "commit", "-m", "Remote edit"}));
  QVERIFY(runGit({"-C", peer, "push", "origin", "HEAD"}));
  {
    NotebookIoGate::ScopedLock lock(gate, id);
    QVERIFY(writeNote(root + QStringLiteral("/note.md"), "local contender\n"));
  }

  QSignalSpy finished(&sync, &SyncService::syncFinished);
  QSignalSpy resolved(&controller, &SyncConflictController::conflictsResolved);
  sync.triggerSyncNow(id);
  QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
  QCOMPARE(qvariant_cast<VxCoreError>(finished.first().at(1)), VXCORE_ERR_SYNC_CONFLICT);
  QTRY_VERIFY(findOpenDialog());
  QCOMPARE(resolved.count(), 0);
  auto *remoteChoice =
      findOpenDialog()->findChild<QRadioButton *>(QStringLiteral("radio_0_remote"));
  QVERIFY(remoteChoice);
  remoteChoice->click();
  auto *accept = findOpenDialog()->findChild<QPushButton *>(QStringLiteral("okButton"));
  QVERIFY(accept);
  accept->click();
  QTRY_COMPARE_WITH_TIMEOUT(resolved.count(), 1, 15000);
  QCOMPARE(resolved.first().at(0).toString(), id);
  QCOMPARE(finished.count(), 2);
  QCOMPARE(qvariant_cast<VxCoreError>(finished.last().at(1)), VXCORE_OK);
  QFile note(root + QStringLiteral("/note.md"));
  QVERIFY(note.open(QIODevice::ReadOnly));
  QCOMPARE(note.readAll(), QByteArray("remote winner\n"));
  note.close();
  QByteArray published;
  QVERIFY(runGit({"--git-dir=" + remote, "show", "main:note.md"}, &published));
  QCOMPARE(published, QByteArray("remote winner\n"));
  sync.shutdown();
  QCOMPARE(notebooks.unregisterSyncRuntime(id), VXCORE_OK);
  QVERIFY(notebooks.closeNotebook(id));
  drainPendingEvents();
}

} // namespace tests

QTEST_MAIN(tests::TestSyncE2E)
#include "test_sync_e2e.moc"
