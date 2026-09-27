// Real sync-dialog behavior: backend-specific fields, strict validation, atomic
// enable failure, and Git username reconfiguration without losing local history.

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

#include <controllers/notebooksyncinfocontroller.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/syncservice.h>
#include <temp_dir_fixture.h>
#include <widgets/dialogs/notebooksyncinfodialog2.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

using namespace vnotex;

namespace tests {

class TestNotebookSyncInfoDialog2 : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void testGitUsernameEditsRemoteWithoutExposingToken();
  void testGitUsernameChangePreservesLocalHistory();
  void testWebdavPreCreateValidationAndSecretMasking();
  void testExistingBackendSelectionAndRawProtection();

  void testFailedBootstrapLeavesNotebookAndDialogIntact();
};

void TestNotebookSyncInfoDialog2::initTestCase() {
  // CRITICAL: enable test mode BEFORE any vxcore_context_create. Mirrors
  // tests/AGENTS.md guidance.
  vxcore_set_test_mode(1);
}

void TestNotebookSyncInfoDialog2::testGitUsernameEditsRemoteWithoutExposingToken() {
  ServiceLocator services;
  NotebookSyncInfoDialog2 dialog(services);
  auto *urlEdit = dialog.findChild<QLineEdit *>(QStringLiteral("remoteUrlEdit"));
  auto *usernameEdit = dialog.findChild<QLineEdit *>(QStringLiteral("gitUsernameEdit"));
  auto *patEdit = dialog.findChild<QLineEdit *>(QStringLiteral("patEdit"));
  QVERIFY(urlEdit);
  QVERIFY(usernameEdit);
  QVERIFY(patEdit);
  urlEdit->setText(QStringLiteral("https://gitee.com/team/notes.git"));
  patEdit->setText(QStringLiteral("test-token"));
  QTest::keyClicks(usernameEdit, "contributor");
  QCOMPARE(dialog.enteredRemoteUrl(),
           QStringLiteral("https://contributor@gitee.com/team/notes.git"));
  QCOMPARE(dialog.enteredSettings().m_credentials.m_secret, QStringLiteral("test-token"));
  QCOMPARE(patEdit->echoMode(), QLineEdit::Password);

  // Loading/resetting the authoritative URL must not retain the previous login.
  urlEdit->setText(QStringLiteral("https://other%40mail.test@gitee.com/team/notes.git"));
  QCOMPARE(usernameEdit->text(), QStringLiteral("other@mail.test"));
  usernameEdit->selectAll();
  QTest::keyClick(usernameEdit, Qt::Key_Backspace);
  QCOMPARE(dialog.enteredRemoteUrl(), QStringLiteral("https://gitee.com/team/notes.git"));
  urlEdit->setText(QStringLiteral("file:///repo.git"));
  QVERIFY(!usernameEdit->isEnabled());
}

void TestNotebookSyncInfoDialog2::testWebdavPreCreateValidationAndSecretMasking() {
  ServiceLocator services;
  NotebookSyncInfoDialog2 dialog(services);
  dialog.show();
  auto *backend = dialog.findChild<QComboBox *>(QStringLiteral("syncBackendCombo"));
  auto *url = dialog.findChild<QLineEdit *>(QStringLiteral("remoteUrlEdit"));
  auto *username = dialog.findChild<QLineEdit *>(QStringLiteral("webdavUsernameEdit"));
  auto *gitUsername = dialog.findChild<QLineEdit *>(QStringLiteral("gitUsernameEdit"));
  auto *secret = dialog.findChild<QLineEdit *>(QStringLiteral("patEdit"));
  auto *ok = dialog.findChild<QPushButton *>(QStringLiteral("okButton"));
  QVERIFY(backend && url && username && gitUsername && secret && ok);
  secret->setText(QStringLiteral("git-token"));
  dialog.setBackend(QStringLiteral("webdav"));
  QVERIFY(backend->isEnabled());
  QVERIFY(username->isVisible());
  QVERIFY(gitUsername->isHidden());
  QVERIFY(secret->text().isEmpty()); // A Git token must not become a DAV password.
  QCOMPARE(secret->echoMode(), QLineEdit::Password);
  QSignalSpy accepted(&dialog, &QDialog::accepted);

  url->setText(QStringLiteral("https://example.com/dav/notebook/"));
  username->setText(QStringLiteral("writer"));
  ok->click();
  QCOMPARE(accepted.count(), 0); // No anonymous enable.
  secret->setText(QStringLiteral("  app password  "));
  url->setText(QStringLiteral("http://127.0.0.1/dav/notebook/"));
  ok->click();
  QCOMPARE(accepted.count(), 0); // UI test mode does not relax HTTPS.
  url->setText(QStringLiteral("https://writer@example.com/dav/notebook/"));
  ok->click();
  QCOMPARE(accepted.count(), 0); // DAV credentials may not leak into a routing URL.
  url->setText(QStringLiteral("https://example.com/dav/notebook/"));
  ok->click();
  QCOMPARE(accepted.count(), 1);
  const auto settings = dialog.enteredSettings();
  QCOMPARE(settings.m_backend, QStringLiteral("webdav"));
  QCOMPARE(settings.m_credentials.m_username, QStringLiteral("writer"));
  QCOMPARE(settings.m_credentials.m_secret, QStringLiteral("  app password  "));
  QCOMPARE(settings.m_remoteUrl, QStringLiteral("https://example.com/dav/notebook/"));
}

void TestNotebookSyncInfoDialog2::testExistingBackendSelectionAndRawProtection() {
  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  {
    ServiceLocator services;
    NotebookCoreService notebooks(ctx);
    services.registerService<NotebookCoreService>(&notebooks);
    TempDirFixture temp;
    QVERIFY(temp.isValid());
    const QString activeId = notebooks.createNotebook(
        temp.createDir("active"),
        QStringLiteral(
            R"({"name":"DAV","syncEnabled":true,"syncBackend":"webdav","syncRemoteUrl":"https://example.com/dav/"})"),
        NotebookType::Bundled);
    QVERIFY(!activeId.isEmpty());
    {
      NotebookSyncInfoDialog2 active(services, activeId);
      auto *backend = active.findChild<QComboBox *>(QStringLiteral("syncBackendCombo"));
      auto *secret = active.findChild<QLineEdit *>(QStringLiteral("patEdit"));
      auto *username = active.findChild<QLineEdit *>(QStringLiteral("webdavUsernameEdit"));
      QVERIFY(backend && secret && username);
      QCOMPARE(backend->currentData().toString(), QStringLiteral("webdav"));
      QVERIFY(!backend->isEnabled());
      QVERIFY(secret->text().isEmpty());
      QVERIFY(username->text().isEmpty()); // No vault read is needed to display settings.
      username->setText(QStringLiteral("different-account"));
      QSignalSpy accepted(&active, &QDialog::accepted);
      active.findChild<QPushButton *>(QStringLiteral("okButton"))->click();
      QCOMPARE(accepted.count(), 0);
      QVERIFY(active.changesPending());
    }
    const QString unknownId = notebooks.createNotebook(
        temp.createDir("unknown"),
        QStringLiteral(
            R"({"name":"Unknown","syncEnabled":true,"syncBackend":"unknown","syncRemoteUrl":"https://example.com/"})"),
        NotebookType::Bundled);
    QVERIFY(!unknownId.isEmpty());
    // The controller reports unsupported/raw configurations synchronously from
    // loadInitialData(), inside the dialog constructor. Drive those real modal
    // errors before inspecting the disabled controls.
    int rejectedConfigurations = 0;
    QTimer dismissConfigurationError;
    connect(&dismissConfigurationError, &QTimer::timeout, this, [&rejectedConfigurations]() {
      if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
        ++rejectedConfigurations;
        box->accept();
      }
    });
    dismissConfigurationError.start(10);
    {
      NotebookSyncInfoDialog2 unknown(services, unknownId);
      QCOMPARE(rejectedConfigurations, 1);
      QCOMPARE(unknown.enteredSettings().m_backend, QStringLiteral("unknown"));
      QVERIFY(!unknown.findChild<QPushButton *>(QStringLiteral("okButton"))->isEnabled());
      QVERIFY(unknown.findChild<QLineEdit *>(QStringLiteral("gitUsernameEdit"))->isHidden());
      QVERIFY(unknown.findChild<QLineEdit *>(QStringLiteral("patEdit"))->isHidden());
    }
    const QString rawId = notebooks.createNotebook(
        temp.createDir("raw"), QStringLiteral(R"({"name":"Raw"})"), NotebookType::Raw);
    QVERIFY(!rawId.isEmpty());
    {
      NotebookSyncInfoDialog2 raw(services, rawId);
      QCOMPARE(rejectedConfigurations, 2);
      QVERIFY(!raw.findChild<QComboBox *>(QStringLiteral("syncBackendCombo"))->isEnabled());
      QVERIFY(!raw.findChild<QPushButton *>(QStringLiteral("okButton"))->isEnabled());
    }
    dismissConfigurationError.stop();
    QVERIFY(notebooks.closeNotebook(rawId));
    QVERIFY(notebooks.closeNotebook(unknownId));
    QVERIFY(notebooks.closeNotebook(activeId));
  }
  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoDialog2::testGitUsernameChangePreservesLocalHistory() {
  class MemoryCredentialsStore : public SyncCredentialsStore {
  public:
    explicit MemoryCredentialsStore(ServiceLocator &p_services)
        : SyncCredentialsStore(p_services) {}
    void storeCredentials(const QString &p_id, const SyncCredential &p_credentials) override {
      QTimer::singleShot(0, this, [this, p_id, p_credentials]() {
        m_credentials = p_credentials;
        emit credentialsStored(p_id);
      });
    }
    void retrieveCredentials(const QString &p_id) override {
      QTimer::singleShot(0, this,
                         [this, p_id]() { emit credentialsRetrieved(p_id, m_credentials); });
    }
    void deleteCredentials(const QString &p_id) override {
      QTimer::singleShot(0, this, [this, p_id]() {
        m_credentials = SyncCredential();
        emit credentialsDeleted(p_id);
      });
    }
    SyncCredential m_credentials;
  };

  VxCoreContextHandle ctx = nullptr;
  QCOMPARE(vxcore_context_create("{}", &ctx), VXCORE_OK);
  {
    ServiceLocator services;
    NotebookCoreService notebookService(ctx);
    services.registerService<NotebookCoreService>(&notebookService);
    MemoryCredentialsStore credentials(services);
    services.registerService<SyncCredentialsStore>(&credentials);
    SyncService syncService(services);
    services.registerService<SyncService>(&syncService);
    TempDirFixture temp;
    QVERIFY(temp.isValid());
    const QString root = temp.filePath(QStringLiteral("username-history"));
    QVERIFY(QDir().mkpath(root));
    // The initial sync after re-enable may try this loopback URL. No external
    // network or real token is used; the re-enable itself must stay local.
    const QString oldUrl = QStringLiteral("https://127.0.0.1:1/team/notes.git");
    const QString newUrl = QStringLiteral("https://contributor@127.0.0.1:1/team/notes.git");
    QJsonObject config{{QStringLiteral("name"), QStringLiteral("Username history")},
                       {QStringLiteral("syncEnabled"), true},
                       {QStringLiteral("syncBackend"), QStringLiteral("git")},
                       {QStringLiteral("syncRemoteUrl"), oldUrl}};
    const QString id = notebookService.createNotebook(
        root, QString::fromUtf8(QJsonDocument(config).toJson()), NotebookType::Bundled);
    QVERIFY(!id.isEmpty());
    const QString gitDir = root + QStringLiteral("/vx_notebook/vx_sync");
    QCOMPARE(QProcess::execute(QStringLiteral("git"),
                               {QStringLiteral("init"), QStringLiteral("--initial-branch=main"),
                                QStringLiteral("--separate-git-dir"), gitDir, root}),
             0);
    const auto runGit = [&](QStringList p_args, QByteArray *p_output = nullptr) {
      QProcess process;
      process.start(QStringLiteral("git"), QStringList{QStringLiteral("--git-dir=") + gitDir,
                                                       QStringLiteral("--work-tree=") + root} +
                                               p_args);
      if (!process.waitForFinished(10000))
        return -1;
      if (p_output)
        *p_output = process.readAllStandardOutput().trimmed();
      return process.exitCode();
    };
    QCOMPARE(
        runGit({QStringLiteral("remote"), QStringLiteral("add"), QStringLiteral("origin"), oldUrl}),
        0);
    QFile note(root + QStringLiteral("/local.md"));
    QVERIFY(note.open(QIODevice::WriteOnly));
    note.write("local content not pushed anywhere\n");
    note.close();
    QCOMPARE(runGit({QStringLiteral("add"), QStringLiteral("local.md")}), 0);
    QCOMPARE(runGit({QStringLiteral("-c"), QStringLiteral("user.name=Test"), QStringLiteral("-c"),
                     QStringLiteral("user.email=test@example.com"), QStringLiteral("commit"),
                     QStringLiteral("-m"), QStringLiteral("local only")}),
             0);
    QCOMPARE(runGit({QStringLiteral("branch"), QStringLiteral("keep-local")}), 0);
    QByteArray originalCommit;
    QCOMPARE(runGit({QStringLiteral("rev-parse"), QStringLiteral("keep-local")}, &originalCommit),
             0);
    const QByteArray syncConfig =
        QJsonDocument(QJsonObject{{QStringLiteral("backend"), QStringLiteral("git")},
                                  {QStringLiteral("remoteUrl"), oldUrl},
                                  {QStringLiteral("autoSyncEnabled"), false}})
            .toJson();
    QCOMPARE(vxcore_sync_enable(ctx, id.toUtf8().constData(), syncConfig.constData(),
                                R"({"pat":"test-token"})"),
             VXCORE_OK);
    credentials.storeCredentials(id,
                                 {QStringLiteral("git"), QString(), QStringLiteral("test-token")});
    QTRY_VERIFY(credentials.hasCredentials(id));

    NotebookSyncInfoDialog2 dialog(services, id);
    auto *controller = dialog.findChild<NotebookSyncInfoController *>();
    auto *username = dialog.findChild<QLineEdit *>(QStringLiteral("gitUsernameEdit"));
    auto *ok = dialog.findChild<QPushButton *>(QStringLiteral("okButton"));
    QVERIFY(controller);
    QVERIFY(username);
    QVERIFY(ok);
    QSignalSpy confirmation(controller, &NotebookSyncInfoController::confirmUrlChangeRequested);
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    dialog.show();
    QTest::keyClicks(username, "contributor");
    ok->click();
    QVERIFY(dialog.isVisible()); // Saved-token retrieval has not finished yet.
    QCOMPARE(confirmation.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(accepted.count(), 1, 10000);
    QVERIFY(syncService.isSyncRegistered(id));
    QCOMPARE(
        notebookService.getNotebookConfig(id).value(QStringLiteral("syncRemoteUrl")).toString(),
        newUrl);
    QByteArray remoteUrl;
    QCOMPARE(runGit({QStringLiteral("config"), QStringLiteral("--get"),
                     QStringLiteral("remote.origin.url")},
                    &remoteUrl),
             0);
    QCOMPARE(remoteUrl, newUrl.toUtf8());
    QByteArray retainedCommit;
    QCOMPARE(runGit({QStringLiteral("rev-parse"), QStringLiteral("keep-local")}, &retainedCommit),
             0);
    QCOMPARE(retainedCommit, originalCommit);
    QVERIFY(note.open(QIODevice::ReadOnly));
    QCOMPARE(note.readAll(), QByteArray("local content not pushed anywhere\n"));
    QCOMPARE(credentials.m_credentials.m_secret, QStringLiteral("test-token"));
    syncService.shutdown();
    QVERIFY(notebookService.closeNotebook(id));
  }
  vxcore_context_destroy(ctx);
}

void TestNotebookSyncInfoDialog2::testFailedBootstrapLeavesNotebookAndDialogIntact() {
  class DeniedCredentialsStore : public SyncCredentialsStore {
  public:
    explicit DeniedCredentialsStore(ServiceLocator &p_services)
        : SyncCredentialsStore(p_services) {}
    void storeCredentials(const QString &p_id, const SyncCredential &) override {
      QTimer::singleShot(0, this, [this, p_id]() {
        emit credentialsStoreError(p_id, QStringLiteral("secure-keychain-unavailable"));
      });
    }
  };

  VxCoreContextHandle context = nullptr;
  QCOMPARE(vxcore_context_create("{}", &context), VXCORE_OK);
  {
    ServiceLocator services;
    NotebookCoreService notebooks(context);
    services.registerService<NotebookCoreService>(&notebooks);
    DeniedCredentialsStore credentials(services);
    services.registerService<SyncCredentialsStore>(&credentials);
    SyncService sync(services);
    services.registerService<SyncService>(&sync);
    TempDirFixture temp;
    QVERIFY(temp.isValid());
    const auto root = temp.createDir("failed-enable");
    const auto id = notebooks.createNotebook(root, QStringLiteral(R"({"name":"Failed enable"})"),
                                             NotebookType::Bundled);
    QVERIFY(!id.isEmpty());
    NotebookSyncInfoDialog2 dialog(services, id);
    dialog.setBootstrapMode(true);
    dialog.setBackend(QStringLiteral("webdav"));
    dialog.show();
    auto *url = dialog.findChild<QLineEdit *>(QStringLiteral("remoteUrlEdit"));
    auto *username = dialog.findChild<QLineEdit *>(QStringLiteral("webdavUsernameEdit"));
    auto *secret = dialog.findChild<QLineEdit *>(QStringLiteral("patEdit"));
    auto *ok = dialog.findChild<QPushButton *>(QStringLiteral("okButton"));
    auto *controller = dialog.findChild<NotebookSyncInfoController *>();
    QVERIFY(url && username && secret && ok && controller);
    url->setText(QStringLiteral("https://example.com/dav/notebook/"));
    username->setText(QStringLiteral("writer"));
    secret->setText(QStringLiteral("app-password"));
    QSignalSpy completed(controller, &NotebookSyncInfoController::applyComplete);
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    QTimer dismissError;
    int dismissedErrors = 0;
    connect(&dismissError, &QTimer::timeout, this, [&]() {
      auto *modal = QApplication::activeModalWidget();
      auto widgets = QApplication::topLevelWidgets();
      widgets.prepend(modal);
      for (auto *widget : widgets) {
        auto *box = qobject_cast<QMessageBox *>(widget);
        if (!box || (!box->isVisible() && box != modal)) {
          continue;
        }
        if (auto *button = box->button(QMessageBox::Ok)) {
          dismissError.stop();
          ++dismissedErrors;
          button->click();
          return;
        }
      }
    });
    dismissError.start(10);
    ok->click();
    QVERIFY(!ok->isEnabled()); // No duplicate enables while the vault operation is pending.
    QTRY_COMPARE(completed.count(), 1);
    dismissError.stop();
    QCOMPARE(dismissedErrors, 1);
    QVERIFY(!completed.first().first().toBool());
    QCOMPARE(accepted.count(), 0);
    QVERIFY(dialog.isVisible());
    QVERIFY(ok->isEnabled());
    QVERIFY(QDir(root).exists());
    QVERIFY(!sync.isSyncRegistered(id));
    const auto config = notebooks.getNotebookConfig(id);
    QVERIFY(!config.value(QStringLiteral("syncEnabled")).toBool());
    QVERIFY(config.value(QStringLiteral("syncRemoteUrl")).toString().isEmpty());
    QCOMPARE(secret->text(), QStringLiteral("app-password")); // Retry retains user input.
    sync.shutdown();
    QVERIFY(notebooks.closeNotebook(id));
  }
  vxcore_context_destroy(context);
}

} // namespace tests

QTEST_MAIN(tests::TestNotebookSyncInfoDialog2)
#include "test_notebooksyncinfodialog2.moc"
